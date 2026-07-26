#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Publish additional mDNS names for THIS host as CNAMEs.

/etc/avahi/hosts cannot do this: it adds an *address* record, and avahi already
owns this machine's IP under its own hostname, so a second name for the same
address is rejected as a "Local name collision". A CNAME points the new name at
the existing one instead, which is what an alias actually is.

Usage: avahi-alias.py <alias> [<alias> ...]
"""

import contextlib
import encodings.idna  # noqa: F401  (idna codec used by .encode)
import signal
import sys

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

CLASS_IN, TYPE_CNAME, TTL = 0x01, 0x05, 60


def encode_name(name: str) -> list[int]:
    """A DNS name in wire form: length-prefixed labels, terminated by a zero."""
    out = bytearray()
    for label in name.rstrip(".").split("."):
        encoded = label.encode("idna") if any(ord(c) > 127 for c in label) else label.encode()
        out.append(len(encoded))
        out += encoded
    out.append(0)
    return list(out)


def main() -> int:
    aliases = sys.argv[1:]
    if not aliases:
        print(__doc__)
        return 2

    DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    server = dbus.Interface(
        bus.get_object("org.freedesktop.Avahi", "/"), "org.freedesktop.Avahi.Server"
    )
    target = server.GetHostNameFqdn()

    group = dbus.Interface(
        bus.get_object("org.freedesktop.Avahi", server.EntryGroupNew()),
        "org.freedesktop.Avahi.EntryGroup",
    )
    for alias in aliases:
        group.AddRecord(
            -1, -1, dbus.UInt32(0), alias, CLASS_IN, TYPE_CNAME, TTL, encode_name(target)
        )
        print(f"publishing {alias} -> {target}", flush=True)
    group.Commit()

    loop = GLib.MainLoop()
    # The records live only as long as this process holds the entry group, so the
    # unit must stay running — exiting withdraws them, which is the correct
    # behaviour when the service stops.
    signal.signal(signal.SIGTERM, lambda *_: loop.quit())
    with contextlib.suppress(KeyboardInterrupt):
        loop.run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
