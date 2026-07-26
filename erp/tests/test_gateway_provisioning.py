# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The gateway provisioning tool (``gateway/provision_identity.py``) end to end.

test_operator_ca.py builds a gateway CSR with ``openssl req`` and notes that the
CA's half of the exchange "is the same one card 11 will drive for real". This
file is that other half: the CSR now comes from the tool, assembled byte by byte
from a public key and a signature over a digest — the contract an ATECC608
actually offers — and then goes through the shipped ceremony and out the far end
as an identity the ERP accepts.

What that pins down:
  * the hand-rolled PKCS#10 DER is a CSR OpenSSL will verify and sign, and
  * the chip's 64-byte R||S becomes a DER signature correctly *including* when a
    component's high bit is set, which is a coin flip per signature rather than
    a rare path, and
  * a certificate issued from the tool's CSR authenticates through the same
    ``mtls.gbox_from_dn`` the ERP derives caller identity with, and
  * the refusals hold: no bare leaf installed, no anchor that is not the root, no
    production binding from a software key, no config write without consent.

Not covered: anything above the signer. There is no ATECC608 and no
cryptoauthlib here, so ``AteccSigner`` and the irreversible lock sequence are
unexercised by construction — the tool says so in its docstring, and a test
claiming otherwise would be lying. What is testable is that everything the chip
feeds is correct, so the only unknown left on real hardware is the chip talking.
"""

from __future__ import annotations

import contextlib
import importlib.util
import json
import shutil
import socket
import ssl
import subprocess
import sys
import threading
from pathlib import Path

import pytest

from app.services import mtls

REPO = Path(__file__).resolve().parents[2]
PKI = REPO / "pki"
TOOL = REPO / "gateway" / "provision_identity.py"
OPERATOR = "OP-STRAWBERRY-01"
ROOT_PW = "pass:root-test-pw"
CA_PW = "pass:issuing-test-pw"

pytestmark = pytest.mark.skipif(
    not (shutil.which("openssl") and shutil.which("bash")),
    reason="needs the openssl the tool signs with and the bash the pki/ scripts are written in",
)


def _load_tool():
    """Import the tool by path — it lives in gateway/, which is not a package.

    Deliberately the shipped file rather than a copy: a change that breaks
    provisioning breaks this suite, the same reason test_operator_ca.py runs the
    pki/ scripts instead of reimplementing them.
    """
    spec = importlib.util.spec_from_file_location("provision_identity", TOOL)
    module = importlib.util.module_from_spec(spec)
    # Registered before execution because the tool's dataclass resolves its
    # string annotations through sys.modules (it uses postponed evaluation).
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


tool = _load_tool()


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(a) for a in args], check=True, capture_output=True, text=True)


# ---------------------------------------------------------------------------
# The CSR the tool builds
# ---------------------------------------------------------------------------


@pytest.fixture
def signer(tmp_path):
    """The manual §5 software fallback, standing in for slot 0.

    It exposes exactly what the chip does — a 64-byte X||Y public key and a
    64-byte R||S signature over a caller-supplied digest — so build_csr is the
    same code here as on a Pi holding a real part.
    """
    return tool.SoftwareSigner.generate(tmp_path / "gbox.key")


def test_the_csr_is_one_openssl_verifies(signer, tmp_path):
    csr = tmp_path / "GBOX_0001.csr"
    csr.write_text(tool.build_csr(signer, "GBOX_0001"))

    # -verify checks the request's self-signature: it proves the signature covers
    # the CertificationRequestInfo the tool assembled and that the key in the CSR
    # is the key that signed it. If any byte of the DER were misplaced, either
    # the parse or this check would fail.
    done = subprocess.run(
        ["openssl", "req", "-in", str(csr), "-noout", "-verify"],
        capture_output=True,
        text=True,
        check=True,
    )
    assert "verify OK" in done.stdout + done.stderr

    subject = run("openssl", "req", "-in", csr, "-noout", "-subject", "-nameopt", "RFC2253").stdout
    assert subject.partition("=")[2].strip() == "CN=GBOX_0001"


def test_the_csr_carries_the_signer_key_and_the_right_curve(signer, tmp_path):
    csr = tmp_path / "c.csr"
    csr.write_text(tool.build_csr(signer, "GBOX_0001"))
    text = run("openssl", "req", "-in", csr, "-noout", "-text").stdout
    assert "prime256v1" in text
    assert "ecdsa-with-SHA256" in text
    # The key in the request is the signer's, not a re-encoding accident.
    in_csr = run("openssl", "req", "-in", csr, "-noout", "-pubkey").stdout
    on_disk = run("openssl", "ec", "-in", signer.key_path, "-pubout").stdout
    assert in_csr.strip() == on_disk.strip()


def test_an_organization_still_leaves_the_cn_where_the_ca_looks_for_it(signer, tmp_path):
    # The manual permits O= for estate consistency. pki/sign-csr.sh greps CN out
    # of the RFC2253 DN, so the RDN order has to put CN leftmost there.
    csr = tmp_path / "c.csr"
    csr.write_text(tool.build_csr(signer, "GBOX_0001", organization=OPERATOR))
    subject = run("openssl", "req", "-in", csr, "-noout", "-subject", "-nameopt", "RFC2253").stdout
    assert subject.partition("=")[2].strip() == f"CN=GBOX_0001,O={OPERATOR}"


def test_the_cn_must_name_a_machine(signer):
    # Caught here rather than at the CA: a CSR whose CN is a hostname produces a
    # certificate that authenticates as nothing, and the failure would surface in
    # the ERP as a rejected caller far from its cause.
    for wrong in ("gbox-dev.local", "GBOX_1", "gbox_0001", "GBOX_00001"):
        with pytest.raises(tool.ProvisioningError, match="GBOX_NNNN"):
            tool.build_csr(signer, wrong)


# ---------------------------------------------------------------------------
# The signature encoding the chip's output needs
# ---------------------------------------------------------------------------


def test_high_bit_components_are_encoded_as_positive_integers():
    # An r or s whose first byte has the high bit set is still a positive number.
    # Without the pad it encodes as negative, and roughly half of all real
    # signatures have at least one such component — so a missing pad is not a
    # rare-path bug, it is a coin flip per signature.
    raw = bytes([0xFF] + [0x11] * 31) + bytes([0x80] + [0x22] * 31)
    der = tool.der_signature(raw)
    assert der[0] == 0x30
    r_body = der[4 : 4 + der[3]]
    assert r_body[0] == 0x00 and r_body[1] == 0xFF
    assert tool.raw_signature(der) == raw


def test_leading_zeros_are_trimmed_and_restored():
    # DER integers are minimal, so a component with leading zero bytes shrinks on
    # the way out and has to be re-padded to 32 bytes on the way back.
    raw = bytes(3) + bytes([0x07] + [0x33] * 28) + bytes([0x01] + [0x44] * 31)
    assert tool.raw_signature(tool.der_signature(raw)) == raw


def test_a_signature_that_is_not_64_bytes_is_refused():
    with pytest.raises(tool.ProvisioningError, match="64-byte"):
        tool.der_signature(bytes(63))


def test_malformed_signatures_do_not_become_csrs():
    for bad in (b"", b"\x02\x01\x00", b"\x30\x03\x02\x01\x00", b"\x30\x00"):
        with pytest.raises(tool.ProvisioningError):
            tool.raw_signature(bad)


def test_a_public_key_of_the_wrong_size_is_refused():
    with pytest.raises(tool.ProvisioningError, match="64-byte"):
        tool.subject_public_key_info(bytes(65))


def test_the_fingerprint_is_over_the_public_key_not_the_certificate(signer):
    # ADR-0007 rev 1 d10d: the anchor has to survive renewal and re-certification
    # under another operator, so it keys on the key, which never changes, rather
    # than on a certificate serial, which changes every 90 days.
    once = tool.public_key_fingerprint(signer.public_key())
    assert once == tool.public_key_fingerprint(signer.public_key())
    assert len(once) == 64
    other = tool.SoftwareSigner.generate(signer.key_path.parent / "other.key")
    assert tool.public_key_fingerprint(other.public_key()) != once


# ---------------------------------------------------------------------------
# The round trip: tool -> operator CA -> the ERP's identity extraction
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def provisioned(tmp_path_factory):
    """A gateway identity provisioned the way an operator would, minus the chip.

    Runs the shipped ceremony (pki/) and the shipped tool against each other: the
    tool builds the CSR, the CA issues from it, the tool installs the result.
    """
    d = tmp_path_factory.mktemp("provisioning")
    ca = d / "ca"

    run(PKI / "bootstrap-root.sh", "--dir", ca, "--operator", OPERATOR, "--pass", ROOT_PW)
    run(PKI / "issue-intermediate.sh", "--dir", ca, "--root-pass", ROOT_PW, "--pass", CA_PW)
    # The ceremony retires the root key offsite; sign-csr.sh refuses to run beside it.
    (ca / "operator-root.key").rename(d / "root-key-offsite.pem")

    key = d / "gbox0001.key"
    csr = d / "GBOX_0001.csr"
    assert (
        tool.main(
            [
                "csr",
                "--gbox",
                "GBOX_0001",
                "--software-key",
                str(key),
                "--organization",
                OPERATOR,
                "--out",
                str(csr),
            ]
        )
        == 0
    )

    run(PKI / "sign-csr.sh", "--dir", ca, "--csr", csr, "--profile", "gateway", "--pass", CA_PW)

    # The ERP needs a server identity from the same CA for the handshake below.
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
        f"/O={OPERATOR}/OU=erp/CN=erp.local",
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

    return {
        "dir": d,
        "ca": ca,
        "key": key,
        "csr": csr,
        "leaf": ca / "issued" / "GBOX_0001.crt",
        "chain": ca / "issued" / "GBOX_0001-chain.crt",
    }


def handshake(ca: Path, client_chain: Path, client_key: Path) -> dict | None:
    """One mutually-authenticated handshake; the server's view of the peer, or None.

    The same arrangement as test_operator_ca.py's: the server anchors on the
    operator root only, so the client must present its chain (ADR-0024 d3).
    """
    server_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server_ctx.load_cert_chain(ca / "server-chain.crt", ca / "server.key")
    server_ctx.verify_mode = ssl.CERT_REQUIRED
    server_ctx.load_verify_locations(ca / "operator-root.crt")

    client_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    client_ctx.load_verify_locations(ca / "operator-root.crt")
    client_ctx.load_cert_chain(client_chain, client_key)

    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    port = listener.getsockname()[1]
    accepted: list[dict] = []

    def serve() -> None:
        raw, _ = listener.accept()
        try:
            with server_ctx.wrap_socket(raw, server_side=True) as tls:
                accepted.append(tls.getpeercert())
        except ssl.SSLError:
            raw.close()

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        with (
            socket.create_connection(("127.0.0.1", port), timeout=5) as raw,
            contextlib.suppress(ssl.SSLError),
        ):
            client_ctx.wrap_socket(raw, server_hostname="localhost").close()
    finally:
        thread.join(timeout=5)
        listener.close()

    return accepted[0] if accepted else None


def test_the_operator_ca_issues_from_the_tools_csr(provisioned):
    assert provisioned["leaf"].is_file()
    issuer = run(
        "openssl", "x509", "-in", provisioned["leaf"], "-noout", "-issuer", "-nameopt", "RFC2253"
    ).stdout
    assert f"CN={OPERATOR} issuing CA" in issuer


def test_the_provisioned_identity_authenticates_and_the_erp_reads_it(provisioned):
    # The whole point of card 11: a certificate whose key was never handled by
    # the CA, requested by the tool, verifying under the operator root and
    # yielding the machine identity the ERP authorises on (ADR-0022 d2).
    peer = handshake(provisioned["ca"], provisioned["chain"], provisioned["key"])
    assert peer is not None, "a leaf issued from the tool's CSR must verify"

    subject = (
        run(
            "openssl",
            "x509",
            "-in",
            provisioned["leaf"],
            "-noout",
            "-subject",
            "-nameopt",
            "RFC2253",
        )
        .stdout.partition("=")[2]
        .strip()
    )
    assert mtls.gbox_from_dn(subject) == "GBOX_0001"


def test_renewal_re_certifies_the_same_key(provisioned):
    # Manual §9: renewal is a new CSR over the *same* key, never a new key. The
    # tool has no code path that regenerates it, and the fingerprint the binding
    # anchors on is unchanged across the renewal.
    renewed = provisioned["dir"] / "renewed.csr"
    assert (
        tool.main(
            [
                "csr",
                "--gbox",
                "GBOX_0001",
                "--software-key",
                str(provisioned["key"]),
                "--out",
                str(renewed),
            ]
        )
        == 0
    )
    before = run("openssl", "req", "-in", provisioned["csr"], "-noout", "-pubkey").stdout
    after = run("openssl", "req", "-in", renewed, "-noout", "-pubkey").stdout
    assert before == after

    run(
        PKI / "sign-csr.sh",
        "--dir",
        provisioned["ca"],
        "--csr",
        renewed,
        "--profile",
        "gateway",
        "--out",
        provisioned["dir"] / "renewed.crt",
        "--pass",
        CA_PW,
    )
    assert (
        handshake(
            provisioned["ca"],
            # The renewed leaf plus the same intermediate: what install would place.
            _chain(provisioned["dir"] / "renewed.crt", provisioned["ca"] / "issuing-ca.crt"),
            provisioned["key"],
        )
        is not None
    )


def _chain(leaf: Path, intermediate: Path) -> Path:
    out = leaf.with_name(leaf.stem + "-chain.crt")
    out.write_text(leaf.read_text() + intermediate.read_text())
    return out


# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------


def test_install_places_the_chain_and_no_private_key(provisioned, tmp_path, capsys):
    target = tmp_path / "etc"
    assert (
        tool.main(
            [
                "install",
                "--gbox",
                "GBOX_0001",
                "--chain",
                str(provisioned["chain"]),
                "--anchor",
                str(provisioned["ca"] / "operator-root.crt"),
                "--dir",
                str(target),
            ]
        )
        == 0
    )

    installed = target / "gateway-chain.crt"
    assert len(tool._certificates_in(installed.read_text())) == 2
    assert (target / "operator-root.crt").is_file()
    # ADR-0007 d1: there is no key file to install, because there is no key file.
    assert {p.name for p in target.iterdir()} == {"gateway-chain.crt", "operator-root.crt"}
    assert "not installed: a private key" in capsys.readouterr().out


def test_install_refuses_the_bare_leaf(provisioned, tmp_path, capsys):
    # The two-tier fallout ADR-0024 turned up: a peer anchored on the root cannot
    # build a path from the leaf alone. test_operator_ca.py proves the handshake
    # fails; this proves the tool will not let you get there.
    assert (
        tool.main(
            [
                "install",
                "--gbox",
                "GBOX_0001",
                "--chain",
                str(provisioned["leaf"]),
                "--dir",
                str(tmp_path / "etc"),
            ]
        )
        == 2
    )
    assert "one certificate" in capsys.readouterr().err
    assert not (tmp_path / "etc" / "gateway-chain.crt").exists()


def test_install_refuses_a_leaf_naming_a_different_machine(provisioned, tmp_path, capsys):
    assert (
        tool.main(
            [
                "install",
                "--gbox",
                "GBOX_0002",
                "--chain",
                str(provisioned["chain"]),
                "--dir",
                str(tmp_path / "etc"),
            ]
        )
        == 2
    )
    assert "authenticate as something else" in capsys.readouterr().err


def test_install_refuses_an_intermediate_as_the_trust_anchor(provisioned, tmp_path, capsys):
    # Anchoring on the issuing CA would trust whatever that CA signs next instead
    # of the root the operator actually controls (ADR-0024 d3).
    assert (
        tool.main(
            [
                "install",
                "--gbox",
                "GBOX_0001",
                "--chain",
                str(provisioned["chain"]),
                "--anchor",
                str(provisioned["ca"] / "issuing-ca.crt"),
                "--dir",
                str(tmp_path / "etc"),
            ]
        )
        == 2
    )
    assert "not self-issued" in capsys.readouterr().err


# ---------------------------------------------------------------------------
# The binding handoff (manual §8)
# ---------------------------------------------------------------------------


def test_the_binding_carries_the_stable_anchors_and_no_secret(provisioned, tmp_path):
    out = tmp_path / "binding.json"
    assert (
        tool.main(
            [
                "binding",
                "--gbox",
                "GBOX_0001",
                "--vendor-serial",
                "SP0004-SN-000123",
                "--software-key",
                str(provisioned["key"]),
                "--cert",
                str(provisioned["leaf"]),
                "--out",
                str(out),
                "--fixture",
            ]
        )
        == 0
    )

    record = json.loads(out.read_text())
    assert record["machine"] == "GBOX_0001"
    assert record["sp0004_vendor_serial"] == "SP0004-SN-000123"
    assert record["public_key_sha256"] == tool.public_key_fingerprint(
        tool.SoftwareSigner(provisioned["key"]).public_key()
    )
    assert record["certificate"]["not_after"]
    assert "fixture" in record  # a software key says so in the record itself
    # Public material only (ADR-0007 d6): nothing key-shaped can be in here.
    assert "PRIVATE KEY" not in out.read_text()


def test_a_software_key_cannot_reach_a_production_binding(provisioned, tmp_path, capsys):
    # Manual §5. A binding asserts a hardware anchor; recording an exportable
    # file key as one makes the binding a claim about a property it lacks.
    assert (
        tool.main(
            [
                "binding",
                "--gbox",
                "GBOX_0001",
                "--vendor-serial",
                "SN1",
                "--software-key",
                str(provisioned["key"]),
                "--out",
                str(tmp_path / "b.json"),
            ]
        )
        == 2
    )
    assert "refusing to write a binding for a software key" in capsys.readouterr().err
    assert not (tmp_path / "b.json").exists()


def test_the_binding_refuses_a_certificate_for_another_machine(provisioned, tmp_path, capsys):
    assert (
        tool.main(
            [
                "binding",
                "--gbox",
                "GBOX_0009",
                "--vendor-serial",
                "SN1",
                "--software-key",
                str(provisioned["key"]),
                "--cert",
                str(provisioned["leaf"]),
                "--fixture",
            ]
        )
        == 2
    )
    assert "names GBOX_0001" in capsys.readouterr().err


# ---------------------------------------------------------------------------
# The config zone (manual §3/§4) — the part that stays untested on hardware
# ---------------------------------------------------------------------------


def test_only_the_two_pinned_words_are_changed():
    # The 608B's full config map is NDA, so the tool patches the part's own
    # factory default rather than writing 128 invented bytes. A wrong byte
    # outside slot 0 locks in exactly as permanently as one inside it.
    present = bytes(range(128))
    wanted, diffs = tool.patch_config_zone(present)

    assert len(wanted) == 128
    changed = {i for i in range(128) if wanted[i] != present[i]}
    assert changed <= {20, 21, 96, 97}
    assert {d.name for d in diffs} == {"SlotConfig[0]", "KeyConfig[0]"}
    # Little-endian in the config zone: 0x2087 is stored 87 20.
    assert wanted[20:22] == b"\x87\x20"
    assert wanted[96:98] == b"\x33\x00"


def test_a_part_already_carrying_the_policy_needs_no_change():
    already, _ = tool.patch_config_zone(bytes(128))
    again, diffs = tool.patch_config_zone(already)
    assert again == already
    assert diffs == []


def test_the_operator_is_shown_every_byte_a_template_would_change():
    # With --config-template the interesting question is not what the §3 words do
    # to the template, it is what the write does to the part on the bench. A
    # template that disagrees elsewhere would otherwise lock those bytes silently.
    present = bytes(128)
    template = bytearray(128)
    template[40] = 0x77  # a byte the manual does not pin
    wanted, _ = tool.patch_config_zone(bytes(template))

    named = {d.offset: d.name for d in tool.config_diffs(present, wanted)}
    # 0x2087 stores as 87 20, so both of SlotConfig[0]'s bytes move; 0x0033 stores
    # as 33 00, so byte 97 is already what it needs to be.
    assert set(named) == {20, 21, 40, 96}
    assert named[20] == named[21] == "SlotConfig[0]"
    assert named[96] == "KeyConfig[0]"
    assert named[40] == ""  # unnamed, but shown — that is the point


def test_a_config_zone_of_the_wrong_size_is_refused():
    with pytest.raises(tool.ProvisioningError, match="128 bytes"):
        tool.patch_config_zone(bytes(127))


def test_keygen_will_not_pretend_to_configure_a_software_key(tmp_path, capsys):
    # There is nothing to lock in a file. Silently succeeding here would let a
    # fixture run look like a provisioning pass.
    assert (
        tool.main(["keygen", "--software-key", str(tmp_path / "k.key"), "--confirm-irreversible"])
        == 2
    )
    assert "nothing to configure or lock" in capsys.readouterr().err


def test_locking_a_slot_requires_saying_so(capsys):
    # No chip is touched: the refusal comes before any I2C traffic, which is the
    # only reason this is testable here at all.
    assert tool.main(["lock-slot"]) == 2
    assert "without --confirm-irreversible" in capsys.readouterr().err


def test_the_atecc_path_says_what_is_missing_rather_than_traceback(capsys):
    # cryptoauthlib is not installed anywhere in this suite. The operator-facing
    # failure should name the two ways forward, not raise ImportError.
    assert tool.main(["csr", "--gbox", "GBOX_0001"]) == 2
    err = capsys.readouterr().err
    assert "cryptoauthlib is not installed" in err
    assert "--software-key" in err
