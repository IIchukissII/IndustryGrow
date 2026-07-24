# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The gateway mTLS channel against real certificates (ADR-0022 d2, ADR-0007).

test_mtls.py exercises the app's half of the seam with hand-written DN strings.
This file removes the hand-writing: it runs the shipped ``make-test-ca.sh`` to
build an actual two-tier PKI and an actual P-256 client certificate, performs a
real TLS handshake with client verification, and feeds the *real* subject DN —
encoded exactly as nginx's ``$ssl_client_s_dn`` encodes it — into the same
extraction the app uses.

What that pins down:
  * a certificate under the operator root completes a verified handshake, and
  * one with the same CN under a foreign root does not (ADR-0007 d3: trust is
    rooted per operator), so a stolen identifier is not an identity, and
  * the DN of the accepted certificate yields exactly ``GBOX_0001``.

This is the *fixture* path — disposable, unencrypted, built in one unattended
pass. test_operator_ca.py runs the real ADR-0024 ceremony in ``pki/`` through the
same seam. Both are two-tier, because that is what deployments run.

Not covered here: nginx itself. The sample config in deploy/mtls/ declares this
contract but is not executed by the suite — see that directory's README.
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

CA_SCRIPT = Path(__file__).resolve().parent.parent / "deploy" / "mtls" / "make-test-ca.sh"

pytestmark = pytest.mark.skipif(
    not (shutil.which("openssl") and shutil.which("bash")),
    reason="needs the openssl and bash used by deploy/mtls/make-test-ca.sh",
)


@pytest.fixture(scope="module")
def pki(tmp_path_factory):
    """A throwaway operator PKI, built by the script operators are pointed at.

    Running the shipped script rather than a test-local reimplementation means a
    change that breaks it breaks the suite too.
    """
    out = tmp_path_factory.mktemp("pki") / "certs"
    subprocess.run([str(CA_SCRIPT), str(out), "GBOX_0001"], check=True, capture_output=True)
    return out


def subject_dn(cert: Path) -> str:
    """The subject DN as nginx puts it in ``$ssl_client_s_dn``.

    nginx prints the name with OpenSSL's RFC 2253 flags; ``-nameopt RFC2253``
    is the same encoding, so this is the exact string the proxy would forward.
    """
    out = subprocess.run(
        ["openssl", "x509", "-in", str(cert), "-noout", "-subject", "-nameopt", "RFC2253"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return out.partition("=")[2].strip()  # strip the leading "subject="


def handshake(pki: Path, client_cert: str, client_key: str) -> dict | None:
    """Complete one mutually-authenticated handshake; return the server's view of
    the peer certificate, or None if verification rejected it.

    The server side is configured the way the proxy is: the operator root is the
    only thing that can vouch for a client.
    """
    server_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    server_ctx.load_cert_chain(pki / "server-chain.crt", pki / "server.key")
    server_ctx.verify_mode = ssl.CERT_REQUIRED
    server_ctx.load_verify_locations(pki / "operator-root.crt")

    client_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    client_ctx.load_verify_locations(pki / "operator-root.crt")
    client_ctx.load_cert_chain(pki / client_cert, pki / client_key)

    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    port = listener.getsockname()[1]

    # Read the peer certificate inside the thread, while the connection is up.
    accepted: list[dict] = []

    def serve() -> None:
        raw, _ = listener.accept()
        try:
            with server_ctx.wrap_socket(raw, server_side=True) as tls:
                accepted.append(tls.getpeercert())
        except ssl.SSLError:
            raw.close()  # the client was refused; that is the result

    thread = threading.Thread(target=serve)
    thread.start()
    try:
        # A rejection surfaces on this side as an alert; the result is read off
        # the server side, which is the half that mirrors the proxy.
        with (
            socket.create_connection(("127.0.0.1", port), timeout=5) as raw,
            contextlib.suppress(ssl.SSLError),
        ):
            client_ctx.wrap_socket(raw, server_hostname="localhost").close()
    finally:
        thread.join(timeout=5)
        listener.close()

    return accepted[0] if accepted else None


def test_the_ca_script_produces_what_the_proxy_needs(pki):
    for name in (
        "operator-root.crt",
        "issuing-ca.crt",
        "server-chain.crt",
        "server.key",
        "gateway-chain.crt",
        "gateway.key",
    ):
        assert (pki / name).is_file(), name


def test_gateway_certificate_is_accepted_under_the_operator_root(pki):
    peer = handshake(pki, "gateway-chain.crt", "gateway.key")
    assert peer is not None, "a certificate issued by the operator root must verify"
    subject = dict(entry for rdn in peer["subject"] for entry in rdn)
    assert subject["commonName"] == "GBOX_0001"


def test_same_identifier_under_a_foreign_root_is_refused(pki):
    # Identical CN, different issuer. ADR-0007 d3 roots trust per operator, so
    # knowing a machine's identifier must not be enough to speak as it.
    assert handshake(pki, "foreign-gateway-chain.crt", "foreign-gateway.key") is None


def test_real_certificate_dn_yields_the_machine_identity(pki):
    assert subject_dn(pki / "gateway.crt") == "CN=GBOX_0001,OU=gateways,O=OP-STRAWBERRY-01"
    assert mtls.gbox_from_dn(subject_dn(pki / "gateway.crt")) == "GBOX_0001"


def test_server_certificate_is_not_a_machine_identity(pki):
    # The ERP's own certificate is issued by the same root. Being under the
    # operator root is necessary, not sufficient — the CN still has to name a
    # machine, so the server leaf cannot be replayed as a gateway.
    with pytest.raises(mtls.GatewayIdentityError):
        mtls.gbox_from_dn(subject_dn(pki / "server.crt"))
