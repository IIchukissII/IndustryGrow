# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The gateway mTLS seam (ADR-0022 d2) — what may be believed, and from whom.

TLS terminates in a proxy, so the app's half of decision 2 is a trust boundary
between two headers and a peer address. These are the ways that boundary can be
attacked or misconfigured. The certificates themselves are exercised in
test_mtls_certs.py.
"""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from app.api.deps import get_warehouse
from app.config import settings
from app.main import create_app
from app.services import mtls

PROXY = "10.9.0.2"
GATEWAY_DN = "CN=GBOX_0001,OU=gateways,O=OP-STRAWBERRY-01"
VERIFIED = {"X-Client-Verify": "SUCCESS", "X-Client-DN": GATEWAY_DN}


@pytest.fixture
def client(monkeypatch, warehouse):
    """The app reached *through* the proxy: peer address 10.9.0.2, which is
    trusted. Tests that need the other side override the peer per request."""
    monkeypatch.setattr(settings, "mongo_mock", True)
    monkeypatch.setattr(settings, "seed_on_start", False)
    monkeypatch.setattr(settings, "gateway_trusted_proxies", ["10.9.0.0/24"])
    app = create_app()
    app.dependency_overrides[get_warehouse] = lambda: warehouse
    with TestClient(app, client=(PROXY, 51000)) as c:
        yield c


# ---- DN parsing: the identity must come out of the certificate --------------


def test_parses_the_rfc2253_dn_nginx_forwards():
    assert mtls.gbox_from_dn(GATEWAY_DN) == "GBOX_0001"


def test_parses_the_legacy_openssl_oneline_dn():
    # nginx < 1.11.6 emits this form; the sample config should not be silently
    # version-locked to the newer one.
    assert mtls.gbox_from_dn("/O=OP-STRAWBERRY-01/OU=gateways/CN=GBOX_0001") == "GBOX_0001"


def test_escaped_separator_cannot_forge_a_different_cn():
    # A CA that issued O="gateways,CN=GBOX_9999" must not yield GBOX_9999: RFC
    # 2253 escapes the comma, and honouring the escape keeps the real CN winning.
    dn = r"CN=GBOX_0001,O=gateways\,CN=GBOX_9999"
    assert mtls.gbox_from_dn(dn) == "GBOX_0001"


@pytest.mark.parametrize(
    "dn",
    [
        "",
        "O=OP-STRAWBERRY-01,OU=gateways",  # no CN at all
        "CN=,O=OP-STRAWBERRY-01",  # empty CN
        "CN=erp.local,O=OP-STRAWBERRY-01",  # a server cert, not a machine
        "CN=gbox_0001,O=OP-STRAWBERRY-01",  # wrong case
        "CN=GBOX_1,O=OP-STRAWBERRY-01",  # wrong arity
        "CN=GBOX_0001-010203,O=OP",  # an integration key, not a machine
    ],
)
def test_rejects_a_dn_that_is_not_a_machine_identifier(dn):
    with pytest.raises(mtls.GatewayIdentityError):
        mtls.gbox_from_dn(dn)


# ---- the peer check --------------------------------------------------------


def test_peer_must_be_a_configured_proxy():
    assert mtls.peer_is_trusted("10.9.0.2", ["10.9.0.0/24"])
    assert mtls.peer_is_trusted("127.0.0.1", ["127.0.0.1"])
    assert not mtls.peer_is_trusted("10.9.1.2", ["10.9.0.0/24"])
    assert not mtls.peer_is_trusted(None, ["10.9.0.0/24"])
    assert not mtls.peer_is_trusted("not-an-address", ["10.9.0.0/24"])


def test_unconfigured_channel_is_not_silently_open():
    with pytest.raises(mtls.GatewayChannelNotConfiguredError):
        mtls.peer_is_trusted("10.9.0.2", [])


# ---- the whole check, in order ---------------------------------------------


def test_untrusted_peer_is_rejected_before_its_headers_are_read():
    # The headers claim a successful verification. They are still worthless,
    # because nobody vouched for whoever sent them.
    with pytest.raises(mtls.GatewayIdentityError):
        mtls.gateway_identity("192.0.2.9", "SUCCESS", GATEWAY_DN, ["10.9.0.0/24"])


@pytest.mark.parametrize("verify", [None, "", "NONE", "FAILED:certificate has expired"])
def test_only_a_successful_verification_counts(verify):
    with pytest.raises(mtls.GatewayIdentityError):
        mtls.gateway_identity(PROXY, verify, GATEWAY_DN, ["10.9.0.0/24"])


def test_verified_peer_yields_the_machine_identity():
    assert mtls.gateway_identity(PROXY, "SUCCESS", GATEWAY_DN, ["10.9.0.0/24"]) == "GBOX_0001"


# ---- through the API -------------------------------------------------------


def _pull(client, **kwargs):
    return client.get("/api/v1/gateway/active-profile", **kwargs)


def test_gateway_channel_is_closed_when_no_proxy_is_configured(client, monkeypatch):
    # 503, not 401: the deployment has no mTLS front end, so the channel does not
    # exist — no credential the caller could present would change that.
    monkeypatch.setattr(settings, "gateway_trusted_proxies", [])
    assert _pull(client, headers=VERIFIED).status_code == 503


def test_forged_headers_from_a_direct_caller_are_rejected(client, warehouse):
    # The hole this closes: reaching the app's port directly and simply asserting
    # an identity. The peer is not the proxy, so nothing is concluded.
    with TestClient(client.app, client=("203.0.113.7", 4444)) as direct:
        assert _pull(direct, headers=VERIFIED).status_code == 401


def test_missing_or_failed_verification_is_rejected(client):
    assert _pull(client).status_code == 401
    assert _pull(client, headers={"X-Client-DN": GATEWAY_DN}).status_code == 401
    assert (
        _pull(client, headers={"X-Client-Verify": "NONE", "X-Client-DN": GATEWAY_DN}).status_code
        == 401
    )
    assert _pull(client, headers={"X-Client-Verify": "SUCCESS"}).status_code == 401


def test_verified_gateway_reaches_the_pull_channel(client):
    # 404 = past authentication and into the route: this GBOX has no recorded
    # active profile. Identity was established, which is what is under test.
    assert _pull(client, headers=VERIFIED).status_code == 404


def test_verified_gateway_pulls_its_own_profile_only(client):
    auth = {"Authorization": "Bearer dev-operator-token"}
    client.post(
        "/api/v1/machines/GBOX_0001/profiles",
        json={"version_tag": "v1", "payload": {"setpoints": {"day_c": 21}}},
        headers=auth,
    )
    client.put(
        "/api/v1/machines/GBOX_0001/active-profile",
        json={"version_tag": "v1"},
        headers=auth,
    )

    mine = _pull(client, headers=VERIFIED)
    assert mine.status_code == 200
    assert mine.json()["machine_id"] == "GBOX_0001"
    assert mine.json()["version_tag"] == "v1"

    # A different verified gateway gets its own (absent) profile, not this one:
    # the identity is the certificate's, and no request parameter overrides it.
    other = _pull(
        client,
        headers={
            "X-Client-Verify": "SUCCESS",
            "X-Client-DN": "CN=GBOX_0002,OU=gateways,O=OP-STRAWBERRY-01",
        },
    )
    assert other.status_code == 404
    assert _pull(client, headers=VERIFIED, params={"gbox": "GBOX_0002"}).json()["version_tag"] == (
        "v1"
    )
