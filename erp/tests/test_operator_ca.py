# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The real operator CA (ADR-0024) against the mTLS seam it exists to feed.

test_mtls_certs.py exercises the same seam with ``deploy/mtls/make-test-ca.sh``,
a throwaway fixture built in one unattended pass with unencrypted keys. This file
runs the *shipped ceremony* in ``pki/`` instead — bootstrap the offline root, have
it sign the issuing intermediate, issue a gateway leaf from that intermediate —
and then puts the result through a real mutually-authenticated handshake.

Both are two-tier; what differs is that this one is the procedure an operator
actually performs, with encrypted keys and the guards that go with them.

What that pins down:
  * the two-tier chain of decision 1 validates end to end, and
  * the intermediate carries decision 2's constraint in the certificate itself
    (CA:TRUE, pathlen:0) rather than by convention, and
  * a peer must present the intermediate; anchoring on the root is not enough
    on its own (decision 3), and
  * the DN of a leaf issued this way still yields ``GBOX_0001``, so the ERP's
    identity extraction is unaffected by the extra tier, and
  * decision 8's "an issuer outlives what it issues" is enforced at issuance.

Not covered here: the offline-ness of decision 5, the two-media custody of
decision 6, and rotation. Those are properties of a procedure performed by a
person, and a test that claimed to verify them would be lying.
"""

from __future__ import annotations

import contextlib
import shutil
import socket
import ssl
import subprocess
import threading
from pathlib import Path

import pytest

from app.services import mtls

PKI = Path(__file__).resolve().parents[2] / "pki"
OPERATOR = "OP-STRAWBERRY-01"
ROOT_PW = "pass:root-test-pw"
CA_PW = "pass:issuing-test-pw"

pytestmark = pytest.mark.skipif(
    not (shutil.which("openssl") and shutil.which("bash")),
    reason="needs the openssl and bash the pki/ ceremony scripts are written in",
)


def run(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(a) for a in args], check=True, capture_output=True, text=True)


@pytest.fixture(scope="module")
def ca(tmp_path_factory):
    """A complete operator CA, built by running the scripts operators are given.

    Running the shipped ceremony rather than a test-local reimplementation means
    a change that breaks the ceremony breaks the suite too — the same reason
    test_mtls_certs.py runs make-test-ca.sh.
    """
    d = tmp_path_factory.mktemp("operator-ca") / "ca"

    run(PKI / "bootstrap-root.sh", "--dir", d, "--operator", OPERATOR, "--pass", ROOT_PW)
    run(PKI / "issue-intermediate.sh", "--dir", d, "--root-pass", ROOT_PW, "--pass", CA_PW)

    # The gateway key stands in for the ATECC608-held key, which by ADR-0007 d1
    # cannot leave the part. Only the CSR ever reaches the CA either way, so the
    # CA's half of the exchange is the same one card 11 will drive for real.
    run(
        "openssl",
        "genpkey",
        "-algorithm",
        "EC",
        "-pkeyopt",
        "ec_paramgen_curve:P-256",
        "-out",
        d / "gateway.key",
    )
    run(
        "openssl",
        "req",
        "-new",
        "-key",
        d / "gateway.key",
        "-subj",
        f"/O={OPERATOR}/OU=gateways/CN=GBOX_0001",
        "-out",
        d / "gateway.csr",
    )

    # The root does not issue leaves (d1) and does not live on an issuing host
    # (d5); the script enforces both, so the ceremony's "retire the root key"
    # step has to happen here before anything can be issued.
    (d / "operator-root.key").rename(d.parent / "root-key-offsite.pem")

    run(
        PKI / "sign-csr.sh",
        "--dir",
        d,
        "--csr",
        d / "gateway.csr",
        "--profile",
        "gateway",
        "--pass",
        CA_PW,
    )
    run(
        "openssl",
        "genpkey",
        "-algorithm",
        "EC",
        "-pkeyopt",
        "ec_paramgen_curve:P-256",
        "-out",
        d / "server.key",
    )
    run(
        "openssl",
        "req",
        "-new",
        "-key",
        d / "server.key",
        "-subj",
        f"/O={OPERATOR}/OU=erp/CN=erp.local",
        "-out",
        d / "server.csr",
    )
    run(
        PKI / "sign-csr.sh",
        "--dir",
        d,
        "--csr",
        d / "server.csr",
        "--profile",
        "server",
        "--san",
        "DNS:erp.local,DNS:localhost,IP:127.0.0.1",
        "--out",
        d / "server.crt",
        "--pass",
        CA_PW,
    )
    return d


def subject_dn(cert: Path) -> str:
    """The subject DN as nginx puts it in ``$ssl_client_s_dn`` (RFC 2253)."""
    out = run("openssl", "x509", "-in", cert, "-noout", "-subject", "-nameopt", "RFC2253").stdout
    return out.partition("=")[2].strip()


def handshake(ca: Path, client_chain: Path, client_key: Path) -> dict | None:
    """One mutually-authenticated handshake; the server's view of the peer, or None.

    The server trusts the operator **root** only — never the intermediate — which
    is decision 3's arrangement and the reason the client has to present a chain.
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
            raw.close()  # refused; that is the result

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


def test_ceremony_produces_a_two_tier_chain(ca):
    assert (ca / "operator-root.crt").is_file()
    assert (ca / "issuing-ca.crt").is_file()
    assert (ca / "issued" / "GBOX_0001.crt").is_file()
    # The leaf's issuer is the intermediate, not the root — d1's whole point.
    issuer = run(
        "openssl",
        "x509",
        "-in",
        ca / "issued" / "GBOX_0001.crt",
        "-noout",
        "-issuer",
        "-nameopt",
        "RFC2253",
    ).stdout
    assert f"CN={OPERATOR} issuing CA" in issuer


def test_intermediate_cannot_mint_further_authorities(ca):
    # ADR-0024 d2, enforced by the certificate rather than by procedure: this CA
    # may sign end-entity certificates and may not sign another CA.
    text = run("openssl", "x509", "-in", ca / "issuing-ca.crt", "-noout", "-text").stdout
    assert "CA:TRUE, pathlen:0" in text
    assert "Certificate Sign" in text


def test_gateway_leaf_verifies_through_the_intermediate(ca):
    peer = handshake(ca, ca / "issued" / "GBOX_0001-chain.crt", ca / "gateway.key")
    assert peer is not None, "a leaf issued under the operator root must verify"
    subject = dict(entry for rdn in peer["subject"] for entry in rdn)
    assert subject["commonName"] == "GBOX_0001"


def test_leaf_alone_is_not_enough(ca):
    # Presenting the leaf without the intermediate leaves the verifier unable to
    # build a path to the anchor it holds. This is why sign-csr.sh emits a
    # -chain.crt and tells the operator to present that one.
    assert handshake(ca, ca / "issued" / "GBOX_0001.crt", ca / "gateway.key") is None


def test_two_tier_dn_still_yields_the_machine_identity(ca):
    dn = subject_dn(ca / "issued" / "GBOX_0001.crt")
    assert dn == f"CN=GBOX_0001,OU=gateways,O={OPERATOR}"
    assert mtls.gbox_from_dn(dn) == "GBOX_0001"


def test_erp_server_leaf_is_not_a_machine_identity(ca):
    # Same issuing CA, but being under the operator root is necessary and not
    # sufficient: the CN still has to name a machine.
    with pytest.raises(mtls.GatewayIdentityError):
        mtls.gbox_from_dn(subject_dn(ca / "server.crt"))


def test_issuance_refuses_a_leaf_that_would_outlive_its_issuer(ca):
    # ADR-0024 d8. Left unchecked this validates on the day it is issued and
    # fails later at the issuer's expiry, pointing at the issuer rather than at
    # the mis-issued leaf.
    with pytest.raises(subprocess.CalledProcessError) as exc:
        run(
            PKI / "sign-csr.sh",
            "--dir",
            ca,
            "--csr",
            ca / "gateway.csr",
            "--profile",
            "gateway",
            "--days",
            "4000",
            "--out",
            ca / "toolong.crt",
            "--pass",
            CA_PW,
        )
    assert "must outlive what it issues" in exc.value.stderr
    assert not (ca / "toolong.crt").exists()


def test_issuance_refuses_to_run_beside_the_root_key(ca, tmp_path):
    # d1/d5: the root does not issue leaves and does not belong on an issuing
    # host. The check is cheap and the mistake is unrecoverable.
    (ca / "operator-root.key").write_text("not the real key")
    try:
        with pytest.raises(subprocess.CalledProcessError) as exc:
            run(
                PKI / "sign-csr.sh",
                "--dir",
                ca,
                "--csr",
                ca / "gateway.csr",
                "--profile",
                "gateway",
                "--out",
                tmp_path / "x.crt",
                "--pass",
                CA_PW,
            )
        assert "does not issue leaves" in exc.value.stderr
    finally:
        (ca / "operator-root.key").unlink()
