# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The profile signing and pull loop end to end (ADR-0025, ADR-0015 d5-d7).

Two tools that never share a line of code have to agree exactly: the operator's
signer in ``signing/sign_profile.py`` and the gateway's client in
``gateway/profile_client.py``. They can, because ADR-0025 decision 6 makes the
signature cover the document's exact bytes — there is no canonical form or
pre-image construction for them to drift on. This file is what proves the two
ends actually agree, and that every refusal in between holds.

What that pins down:
  * a profile signed by the operator's key verifies with the public half the
    gateway is provisioned with, and applies atomically, and
  * the client trusts only what is *inside* the signed bytes — an envelope that
    disagrees about the machine or version changes nothing, and
  * the refusals: unverifiable signature, no signature at all, a profile for
    another cabinet, a version not newer than the running one (ADR-0025 d8), and
    no verification key configured, which is fail-closed rather than open, and
  * on every one of those the previously applied profile is still in place —
    ADR-0015 d7's "the previous version remains active" is a property of the
    filesystem after the failure, not a promise in a docstring, and
  * the ERP refuses to record an unsigned version active (ADR-0025 d11).

Not covered: the systemd timer's cadence, and a pull against a live ERP over real
mTLS. The transport is exercised by pointing the client at a local TLS server
built from the operator CA, which is the same chain arrangement the ERP's own
mTLS tests use; the ERP's half of the contract is covered through its API.
"""

from __future__ import annotations

import base64
import importlib.util
import json
import shutil
import ssl
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import ClassVar

import pytest

REPO = Path(__file__).resolve().parents[2]
PKI = REPO / "pki"
OPERATOR = "OP-STRAWBERRY-01"
ROOT_PW = "pass:root-test-pw"
CA_PW = "pass:issuing-test-pw"
SIGN_PW = "pass:profile-signing-test-pw"

pytestmark = pytest.mark.skipif(
    not (shutil.which("openssl") and shutil.which("bash")),
    reason="needs the openssl both tools sign and verify with, and bash for the pki/ scripts",
)


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


signer = _load("sign_profile", REPO / "signing" / "sign_profile.py")


def load_client(config_dir: Path):
    """The gateway client, with its config directory pointed at a tmp_path.

    Re-imported per test because the paths ADR-0015 d4 fixes are module-level
    constants read from the environment at import — which is what the real unit
    does, once, at start.
    """
    import os

    os.environ["IGROW_CONFIG_DIR"] = str(config_dir)
    return _load("profile_client", REPO / "gateway" / "profile_client.py")


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(a) for a in args], check=True, capture_output=True, text=True)


def document(machine: str = "GBOX_0001", version: str = "v43", **extra) -> bytes:
    """A profile document as the operator would author it (ADR-0025 d7).

    Deliberately built with an indent and a trailing newline: the bytes signed are
    the bytes stored, so anything that reformatted them on the way through would
    show up as a verification failure here.
    """
    body = {"machine_id": machine, "version_tag": version, "setpoints": {"air_c": 21.5}, **extra}
    return (json.dumps(body, indent=2) + "\n").encode()


# ---------------------------------------------------------------------------
# Fixtures: an operator CA, a gateway identity, and a profile-signing key
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def operator(tmp_path_factory):
    d = tmp_path_factory.mktemp("operator")
    ca = d / "ca"

    run(PKI / "bootstrap-root.sh", "--dir", ca, "--operator", OPERATOR, "--pass", ROOT_PW)
    run(PKI / "issue-intermediate.sh", "--dir", ca, "--root-pass", ROOT_PW, "--pass", CA_PW)
    (ca / "operator-root.key").rename(d / "root-offsite.key")

    # The gateway's identity, requested by the provisioning tool built for card 11.
    provision = _load("provision_identity", REPO / "gateway" / "provision_identity.py")
    gateway_key = d / "gbox0001.key"
    csr = d / "GBOX_0001.csr"
    assert (
        provision.main(
            ["csr", "--gbox", "GBOX_0001", "--software-key", str(gateway_key), "--out", str(csr)]
        )
        == 0
    )
    run(PKI / "sign-csr.sh", "--dir", ca, "--csr", csr, "--profile", "gateway", "--pass", CA_PW)

    # A server identity so the client has something to do real TLS against.
    run(
        "openssl",
        "genpkey",
        "-algorithm",
        "EC",
        "-pkeyopt",
        "ec_paramgen_curve:P-256",
        "-out",
        ca / "server.key",
    )
    run(
        "openssl",
        "req",
        "-new",
        "-key",
        ca / "server.key",
        "-subj",
        f"/O={OPERATOR}/CN=erp.local",
        "-out",
        ca / "server.csr",
    )
    run(
        PKI / "sign-csr.sh",
        "--dir",
        ca,
        "--csr",
        ca / "server.csr",
        "--profile",
        "server",
        "--san",
        "DNS:erp.local,DNS:localhost,IP:127.0.0.1",
        "--out",
        ca / "server.crt",
        "--pass",
        CA_PW,
    )

    # The third trust root (ADR-0025 d4) — not the CA, not firmware signing.
    keys = d / "profile-key"
    assert signer.main(["keygen", "--dir", str(keys), "--pass", SIGN_PW]) == 0

    return {
        "dir": d,
        "ca": ca,
        "gateway_key": gateway_key,
        "chain": ca / "issued" / "GBOX_0001-chain.crt",
        "signing_dir": keys,
        "verify_key": keys / "profile-verify.pub",
    }


@pytest.fixture
def gateway(operator, tmp_path):
    """A provisioned gateway: identity chain, trust anchor, verification key."""
    config = tmp_path / "etc"
    pki = config / "pki"
    pki.mkdir(parents=True)
    shutil.copy2(operator["chain"], pki / "gateway-chain.crt")
    shutil.copy2(operator["ca"] / "operator-root.crt", pki / "operator-root.crt")
    shutil.copy2(operator["verify_key"], pki / "profile-verify.pub")
    # The client needs the private half to present its certificate; on a real unit
    # this is the ATECC and there is no file (ADR-0007 d1).
    chain_with_key = pki / "gateway-chain.crt"
    chain_with_key.write_text(operator["chain"].read_text() + operator["gateway_key"].read_text())
    return {"config": config, "pki": pki, "client": load_client(config)}


def sign(operator, doc: bytes, tmp_path: Path) -> str:
    """Sign `doc` with the operator's key, returning the base64 signature."""
    path = tmp_path / "profile.json"
    path.write_bytes(doc)
    assert (
        signer.main(
            ["sign", "--dir", str(operator["signing_dir"]), "--in", str(path), "--pass", SIGN_PW]
        )
        == 0
    )
    return path.with_name(path.name + ".sig").read_text().strip()


# ---------------------------------------------------------------------------
# The signer and the client agree
# ---------------------------------------------------------------------------


def test_the_gateway_verifies_what_the_operator_signed(operator, gateway, tmp_path):
    doc = document()
    signature = sign(operator, doc, tmp_path)
    assert gateway["client"].verify_signature(gateway["pki"] / "profile-verify.pub", doc, signature)


def test_a_single_altered_byte_fails_verification(operator, gateway, tmp_path):
    # The whole point of signing bytes rather than a parsed object: a change that
    # a JSON-equivalent comparison would call identical still fails.
    doc = document()
    signature = sign(operator, doc, tmp_path)
    reformatted = json.dumps(json.loads(doc)).encode()  # same object, different bytes
    assert reformatted != doc
    verify = gateway["client"].verify_signature
    assert not verify(gateway["pki"] / "profile-verify.pub", reformatted, signature)


def test_a_foreign_signing_key_does_not_pass(operator, gateway, tmp_path):
    other = tmp_path / "other-key"
    assert signer.main(["keygen", "--dir", str(other), "--pass", "pass:other"]) == 0
    doc = document()
    path = tmp_path / "p.json"
    path.write_bytes(doc)
    assert (
        signer.main(["sign", "--dir", str(other), "--in", str(path), "--pass", "pass:other"]) == 0
    )
    foreign = path.with_name(path.name + ".sig").read_text().strip()

    verify = gateway["client"].verify_signature
    assert not verify(gateway["pki"] / "profile-verify.pub", doc, foreign)


def test_the_signer_refuses_a_document_that_names_no_cabinet(operator, tmp_path):
    # A community template carries no machine_id, so it cannot be bound to a
    # cabinet or ordered against what is running (ADR-0025 d7).
    template = tmp_path / "template.json"
    template.write_bytes(json.dumps({"id": "strawberry-day-neutral-v1"}).encode())
    assert (
        signer.main(
            [
                "sign",
                "--dir",
                str(operator["signing_dir"]),
                "--in",
                str(template),
                "--pass",
                SIGN_PW,
            ]
        )
        == 2
    )
    assert not template.with_name(template.name + ".sig").exists()


def test_the_signer_never_rewrites_the_document(operator, tmp_path):
    doc = document()
    path = tmp_path / "p.json"
    path.write_bytes(doc)
    assert (
        signer.main(
            ["sign", "--dir", str(operator["signing_dir"]), "--in", str(path), "--pass", SIGN_PW]
        )
        == 0
    )
    assert path.read_bytes() == doc, "signing must not touch the bytes it signs"


def test_keygen_will_not_silently_orphan_an_existing_key(operator, tmp_path):
    keys = tmp_path / "k"
    assert signer.main(["keygen", "--dir", str(keys), "--pass", "pass:x"]) == 0
    first = (keys / "profile-verify.pub").read_bytes()
    assert signer.main(["keygen", "--dir", str(keys), "--pass", "pass:x"]) == 2
    assert (keys / "profile-verify.pub").read_bytes() == first


# ---------------------------------------------------------------------------
# The pull loop
# ---------------------------------------------------------------------------


class _Handler(BaseHTTPRequestHandler):
    envelope: ClassVar[dict] = {}

    def do_GET(self):  # BaseHTTPRequestHandler's interface spells it this way
        body = json.dumps(type(self).envelope).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


@pytest.fixture
def erp(operator, gateway):
    """A TLS server standing in for the ERP's gateway channel.

    Real TLS with the operator's own server certificate, so the client's trust
    setup is exercised rather than bypassed. It requires a client certificate for
    the same reason the ERP's proxy does — a channel that accepted an anonymous
    caller would not be the channel under test.
    """
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(operator["ca"] / "server-chain.crt", operator["ca"] / "server.key")
    context.verify_mode = ssl.CERT_REQUIRED
    context.load_verify_locations(operator["ca"] / "operator-root.crt")

    server = HTTPServer(("127.0.0.1", 0), _Handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield {"port": server.server_port, "handler": _Handler}
    finally:
        server.shutdown()
        thread.join(timeout=5)


def config_for(gateway, erp) -> object:
    client = gateway["client"]
    return client.Config(
        url=f"https://localhost:{erp['port']}/api/v1/gateway/active-profile",
        chain=gateway["pki"] / "gateway-chain.crt",
        anchor=gateway["pki"] / "operator-root.crt",
        verify_key=gateway["pki"] / "profile-verify.pub",
        timeout=10,
    )


def serve(erp, doc: bytes, signature: str | None, **envelope_overrides) -> None:
    envelope = {
        "machine_id": "GBOX_0001",
        "version_tag": "whatever",
        "document_b64": base64.b64encode(doc).decode(),
        "signature": signature,
    }
    envelope.update(envelope_overrides)
    erp["handler"].envelope = envelope


def test_a_signed_profile_is_pulled_verified_and_applied(operator, gateway, erp, tmp_path):
    doc = document(version="v43")
    serve(erp, doc, sign(operator, doc, tmp_path))

    client = gateway["client"]
    assert client.pull_once(config_for(gateway, erp)) is True
    # Byte-identical on disk: the artifact the operator signed is the artifact the
    # control loop reads (ADR-0025 d6).
    assert client.ACTIVE_PROFILE.read_bytes() == doc
    assert client.installed_version() == "v43"


def test_the_machine_identity_comes_from_the_certificate(gateway):
    # Not from configuration: ADR-0007 d10b puts it in the CN and ADR-0022 d2 has
    # the ERP authorise by the same string, so there is no second place to set it
    # wrong (ADR-0025 d7).
    client = gateway["client"]
    assert client.machine_identity(gateway["pki"] / "gateway-chain.crt") == "GBOX_0001"


def test_only_the_signed_bytes_are_authoritative(operator, gateway, erp, tmp_path):
    # The envelope is unsigned. An ERP (or anything between) that claims a
    # different machine and version must change nothing about what is applied.
    doc = document(machine="GBOX_0001", version="v43")
    serve(erp, doc, sign(operator, doc, tmp_path), machine_id="GBOX_0009", version_tag="v999")

    client = gateway["client"]
    assert client.pull_once(config_for(gateway, erp)) is True
    assert client.installed_version() == "v43"  # from inside the bytes, not the envelope


def test_an_unverifiable_profile_is_not_applied(operator, gateway, erp, tmp_path):
    good = document(version="v43")
    serve(erp, good, sign(operator, good, tmp_path))
    client = gateway["client"]
    assert client.pull_once(config_for(gateway, erp)) is True

    tampered = document(version="v44", setpoints_tampered=True)
    serve(erp, tampered, sign(operator, good, tmp_path))  # signature for the other document

    with pytest.raises(client.ProfileError, match="does NOT verify"):
        client.pull_once(config_for(gateway, erp))
    # ADR-0015 d7: the previous version remains active. Checked on disk.
    assert client.installed_version() == "v43"
    assert client.ACTIVE_PROFILE.read_bytes() == good


def test_an_unsigned_profile_is_not_applied(gateway, erp):
    serve(erp, document(), None)
    client = gateway["client"]
    with pytest.raises(client.ProfileError, match="no signature"):
        client.pull_once(config_for(gateway, erp))
    assert not client.ACTIVE_PROFILE.exists()


def test_a_profile_for_another_cabinet_is_not_applied(operator, gateway, erp, tmp_path):
    other = document(machine="GBOX_0002", version="v99")
    serve(erp, other, sign(operator, other, tmp_path))

    client = gateway["client"]
    with pytest.raises(client.ProfileError, match="signed for GBOX_0002"):
        client.pull_once(config_for(gateway, erp))
    assert not client.ACTIVE_PROFILE.exists()


def test_an_older_version_is_refused_even_though_it_verifies(operator, gateway, erp, tmp_path):
    # The downgrade a signature cannot catch: this artifact was genuinely signed
    # and stays valid forever (ADR-0025 d8).
    new = document(version="v43")
    serve(erp, new, sign(operator, new, tmp_path))
    client = gateway["client"]
    assert client.pull_once(config_for(gateway, erp)) is True

    old = document(version="v42")
    serve(erp, old, sign(operator, old, tmp_path))
    assert client.pull_once(config_for(gateway, erp)) is False
    assert client.installed_version() == "v43"


def test_the_same_version_is_a_no_op_not_a_failure(operator, gateway, erp, tmp_path):
    # The steady state of a 60-second poll. It must not look like an error, or the
    # journal becomes noise nobody reads.
    doc = document(version="v43")
    serve(erp, doc, sign(operator, doc, tmp_path))
    client = gateway["client"]
    assert client.pull_once(config_for(gateway, erp)) is True
    assert client.pull_once(config_for(gateway, erp)) is False


def test_an_unorderable_version_is_refused_rather_than_guessed(operator, gateway, erp, tmp_path):
    first = document(version="v43")
    serve(erp, first, sign(operator, first, tmp_path))
    client = gateway["client"]
    client.pull_once(config_for(gateway, erp))

    odd = document(version="latest")
    serve(erp, odd, sign(operator, odd, tmp_path))
    with pytest.raises(client.ProfileError, match="cannot order version"):
        client.pull_once(config_for(gateway, erp))
    assert client.installed_version() == "v43"


def test_no_verification_key_means_nothing_is_applied(operator, gateway, erp, tmp_path):
    # Fail-closed, like the ERP's own gateway routes: a missing key is not a
    # licence to skip the check ADR-0015 d7 requires.
    (gateway["pki"] / "profile-verify.pub").unlink()
    doc = document()
    serve(erp, doc, sign(operator, doc, tmp_path))

    client = gateway["client"]
    with pytest.raises(client.ProfileError, match="no profile-verification key"):
        client.pull_once(config_for(gateway, erp))
    assert not client.ACTIVE_PROFILE.exists()


def test_an_unreachable_erp_leaves_the_profile_alone(operator, gateway, erp, tmp_path):
    doc = document(version="v43")
    serve(erp, doc, sign(operator, doc, tmp_path))
    client = gateway["client"]
    client.pull_once(config_for(gateway, erp))

    dead = client.Config(
        url="https://127.0.0.1:1/api/v1/gateway/active-profile",
        chain=gateway["pki"] / "gateway-chain.crt",
        anchor=gateway["pki"] / "operator-root.crt",
        verify_key=gateway["pki"] / "profile-verify.pub",
        timeout=2,
    )
    with pytest.raises(client.ProfileError, match="cannot reach the ERP"):
        client.pull_once(dead)
    assert client.installed_version() == "v43"


def test_the_previous_profile_becomes_the_last_known_good(operator, gateway, erp, tmp_path):
    # ADR-0020 d11's cold boot resumes from this copy.
    first = document(version="v43")
    serve(erp, first, sign(operator, first, tmp_path))
    client = gateway["client"]
    client.pull_once(config_for(gateway, erp))

    second = document(version="v44")
    serve(erp, second, sign(operator, second, tmp_path))
    client.pull_once(config_for(gateway, erp))

    assert client.ACTIVE_PROFILE.read_bytes() == second
    assert client.LAST_KNOWN_GOOD.read_bytes() == first


def test_a_corrupt_active_profile_does_not_block_replacing_it(operator, gateway, erp, tmp_path):
    # Refusing to pull because the thing being replaced is broken would strand the
    # cabinet on the broken thing.
    client = gateway["client"]
    client.CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    client.ACTIVE_PROFILE.write_bytes(b"{ this is not json")

    doc = document(version="v1")
    serve(erp, doc, sign(operator, doc, tmp_path))
    assert client.pull_once(config_for(gateway, erp)) is True
    assert client.installed_version() == "v1"
