# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
r"""The gateway mTLS channel — deriving a machine identity from a verified cert.

ADR-0022 decision 2: gateway machine callers authenticate by mTLS against the
ADR-0007 operator root, and the identity ``GBOX_NNNN`` is derived **from the
verified certificate, never from a request parameter**.

TLS terminates in a reverse proxy, not here (the app speaks plain HTTP inside the
deployment). That splits the decision across two components, so the seam between
them is what this module defends:

  proxy   verifies the client chain against the operator root and forwards the
          *verification result* and the certificate's *subject DN* as headers,
          after stripping any inbound copies of those headers.
  app     accepts them only from a configured proxy address, requires the result
          to be a success, and parses the identifier out of the DN itself.

Both halves are load-bearing. Parsing the DN here rather than letting the proxy
send a ready-made ``X-Client-Gbox`` keeps decision 2's "never from a request
parameter" true of the *whole* path: there is no header a caller could set that
becomes an identity, only a DN that must have come from a certificate the proxy
verified.

The trusted-proxy list has no default. An unconfigured deployment refuses the
gateway channel outright rather than trusting whatever arrives — the header
contract is only as good as the guarantee that a proxy, and not a client, wrote
it, and there is no safe guess about which peer that is.
"""

from __future__ import annotations

import ipaddress
import re

from app.models.identifiers import MACHINE_RE

# nginx sets $ssl_client_verify to SUCCESS, FAILED:<reason>, or NONE.
VERIFY_SUCCESS = "SUCCESS"

Network = ipaddress.IPv4Network | ipaddress.IPv6Network


class GatewayIdentityError(Exception):
    """The forwarded material does not establish a gateway identity."""


class GatewayChannelNotConfiguredError(Exception):
    """No trusted proxy is configured, so no forwarded identity can be believed."""


def parse_trusted(entries: list[str]) -> list[Network]:
    """Config strings -> networks. A bare address is its own /32 or /128."""
    return [ipaddress.ip_network(e.strip(), strict=False) for e in entries if e.strip()]


def peer_is_trusted(peer: str | None, trusted: list[str]) -> bool:
    """Is ``peer`` (the direct TCP peer) one of the configured proxy addresses?

    This must be the *transport* peer. Uvicorn's ``--proxy-headers`` mode rewrites
    it from ``X-Forwarded-For``, which would let a caller choose the address this
    check sees, so the app runs with that mode off (see ``main.run`` and the
    container CMD).

    Raises rather than returning False when nothing is configured: "no proxy is
    trusted" and "this peer is not the proxy" are different operational states
    and deserve different answers to the caller.
    """
    if not trusted:
        raise GatewayChannelNotConfiguredError(
            "no trusted mTLS proxy configured (ERP_GATEWAY_TRUSTED_PROXIES)"
        )
    if not peer:
        return False
    try:
        address = ipaddress.ip_address(peer)
    except ValueError:
        return False
    return any(address in network for network in parse_trusted(trusted))


def parse_dn(dn: str) -> dict[str, str]:
    r"""Parse a certificate subject DN into its attributes, last value winning.

    Two formats, because nginx changed its mind: RFC 2253 (``CN=a,OU=b``, what
    ``$ssl_client_s_dn`` has emitted since 1.11.6) and the older OpenSSL "oneline"
    (``/OU=b/CN=a``). Accepting both means the sample config is not silently
    version-locked. RFC 2253 escaping (``\,`` and friends) is honoured, so a value
    containing a separator cannot be split into a different identity.
    """
    text = dn.strip()
    if not text:
        return {}

    # An RFC 2253 DN begins with an attribute type, never a slash.
    parts = _split_unescaped(text[1:], "/") if text.startswith("/") else _split_unescaped(text, ",")

    attributes: dict[str, str] = {}
    for part in parts:
        key, sep, value = part.partition("=")
        if sep:
            attributes[key.strip().upper()] = _unescape(value.strip())
    return attributes


def _split_unescaped(text: str, separator: str) -> list[str]:
    parts: list[str] = []
    current: list[str] = []
    escaped = False
    for char in text:
        if escaped:
            current.append(char)
            escaped = False
        elif char == "\\":
            escaped = True
        elif char == separator:
            parts.append("".join(current))
            current = []
        else:
            current.append(char)
    if escaped:  # trailing backslash — keep it rather than silently drop it
        current.append("\\")
    parts.append("".join(current))
    return [p for p in parts if p]


def _unescape(value: str) -> str:
    return re.sub(r"\\(.)", r"\1", value)


def gbox_from_dn(dn: str) -> str:
    """The machine identifier carried by a gateway certificate: its subject CN.

    ADR-0007 fixes what a gateway certificate *is* but not how it spells the unit
    it belongs to; this is where that is pinned down. The CN is the ADR-0017
    machine identifier verbatim (``GBOX_0001``) — no prefix, no serial, no email
    form — so the string the proxy verified is the string the ERP keys on, with
    no translation step in between to get wrong.
    """
    common_name = parse_dn(dn).get("CN")
    if not common_name:
        raise GatewayIdentityError("client certificate subject has no CN")
    if not MACHINE_RE.match(common_name):
        raise GatewayIdentityError(
            f"certificate CN {common_name!r} is not a machine identifier (GBOX_NNNN)"
        )
    return common_name


def gateway_identity(
    peer: str | None, verify: str | None, dn: str | None, trusted: list[str]
) -> str:
    """The whole check, in order: trusted peer, successful verification, then DN.

    Order matters. Until the peer is known to be the proxy, the other two headers
    are only caller-supplied strings and nothing may be concluded from them —
    including, in particular, that a verification *failed* rather than never
    happened at all.
    """
    if not peer_is_trusted(peer, trusted):
        raise GatewayIdentityError("request did not arrive through the mTLS proxy")
    if (verify or "").strip().upper() != VERIFY_SUCCESS:
        raise GatewayIdentityError("client certificate was not verified by the proxy")
    if not dn:
        raise GatewayIdentityError("verified client certificate carries no subject DN")
    return gbox_from_dn(dn)
