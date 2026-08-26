#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""IndustryGrow gateway telemetry consumer -- the Pycyphal/SocketCAN edge.

Subscribes to the node telemetry subjects, decodes them through the DSDL
vocabulary (ADR-0005), stamps each sample with the provenance ADR-0004 decision
18 fixes, and writes it to the bounded local store of ADR-0020 decision 2. Before
IndustryFlow exists that store is not a buffer, it is the record (ADR-0020
decision 1).

THREE STAMPS, ADR-0004 d18. `t_acq` is the node's own bus-time stamp and the only
one on the wire; `0` means UNKNOWN and is stored as `0` -- substituting `t_rx`
would make the acquisition-to-receipt latency read zero forever. `t_rx` is the
kernel's reception timestamp, kept in both CLOCK_REALTIME and CLOCK_MONOTONIC
form. `t_store` is stamped when the row is actually written, so batching cannot
turn it into a guess; with the store off there is no local write and it is
absent, not zero.

THE LATENCY IS WITHHELD, NOT CLAMPED. `t_rx - t_acq` spans two machines and has
no monotonic form (ADR-0004 d21), so a correction to the host clock reaches the
two ends of the subtraction at different times and reads as latency never
incurred. It is recorded as NULL while the host clock is unsynchronized or was
stepped recently. Both stamps are still stored; only the derived value is
withheld.

ANONYMOUS ON THE BUS. This unit only subscribes, so it takes no Node-ID. The
gateway's Node-ID (ADR-0002 d11) belongs to the time master, which is a separate
unit; two units claiming one ID would be a defect, and a consumer that never
transmits does not need one.

Run modes:
  --serve         (default) consume until stopped.
  --once SECONDS  consume for SECONDS, print a summary, exit -- a manual check.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import ctypes
import json
import os
import signal
import sqlite3
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# --- Configuration -----------------------------------------------------------
# Every value an operator may need to change is an environment variable in
# /etc/industrygrow/gateway.env; none of them is a decision (ADR-0000 d2).


def _env(name: str, default: str) -> str:
    """An empty value means the default: EnvironmentFile passes blanks through."""
    return os.environ.get(name) or default


CAN_IFACE = _env("IGROW_CAN_IFACE", "vcan0")
STATE_DIR = Path(_env("STATE_DIRECTORY", "/var/lib/industrygrow").split(":")[0])
# The store gate is the one that already exists: ADR-0020 d10 permits bare SD
# only for a deployment with no local store, so it is OFF until an operator
# declares an SSD/NVMe medium.
STORE_ENABLED = _env("IGROW_PERSISTENT_BUFFER", "off").lower() in ("on", "1", "true", "yes")
DB_PATH = Path(_env("IGROW_TELEMETRY_DB", str(STATE_DIR / "telemetry.sqlite3")))

# ADR-0020 d2: the bound is a TIME bound, not a capacity bound. ~7 days is the
# working starting point -- a long weekend plus a multi-day outage with margin.
RETENTION_DAYS = float(_env("IGROW_RETENTION_DAYS", "7"))
RETENTION_SWEEP_S = float(_env("IGROW_RETENTION_SWEEP_S", "3600"))

COMMIT_INTERVAL_S = float(_env("IGROW_COMMIT_INTERVAL_S", "5"))
COMMIT_ROWS = int(_env("IGROW_COMMIT_ROWS", "200"))
REPORT_S = float(_env("IGROW_REPORT_S", "60"))

# A heartbeat at 1 Hz per node is not a record of anything until it changes.
# Stored on change, plus one keepalive per interval so a silent gap is bounded.
HEARTBEAT_KEEPALIVE_S = float(_env("IGROW_HEARTBEAT_KEEPALIVE_S", "300"))

# ADR-0004 d21. A node re-derives its time offset at most once every two master
# publication periods (ADR-0002 d11, 0.9 s), and the host clock takes the
# disciplining interval to settle after a step -- about a minute on the
# reference gateway while systemd-timesyncd corrected a 10 ms RTC offset.
SYNC_PUBLICATION_PERIOD_S = 0.9
CLOCK_SETTLE_S = float(_env("IGROW_CLOCK_SETTLE_S", "60"))
LATENCY_BLACKOUT_S = (2.0 * SYNC_PUBLICATION_PERIOD_S) + CLOCK_SETTLE_S
CLOCK_STEP_NS = int(float(_env("IGROW_CLOCK_STEP_MS", "5")) * 1e6)

HEARTBEAT_SUBJECT_ID = 7509
DIAGNOSTIC_SUBJECT_ID = 8184


def log(msg: str) -> None:
    # stdout is captured by journald for this unit (ADR-0004 d11).
    print(f"[gateway-telemetry] {msg}", flush=True)


# --- Host clock discipline ---------------------------------------------------


class ClockGuard:
    """Whether `t_rx - t_acq` may be recorded at all (ADR-0004 d21).

    Two independent disqualifiers: the host clock is not disciplined, or it was
    stepped inside the settling window. The step detector needs no syscall --
    CLOCK_REALTIME minus CLOCK_MONOTONIC is constant under slewing and jumps only
    when the clock is stepped, and both readings arrive with every frame.
    """

    # adjtimex() return value when STA_UNSYNC is set.
    TIME_ERROR = 5

    def __init__(self) -> None:
        self._offset_ns: int | None = None
        self._last_step_mono_ns: int | None = None
        self._steps = 0
        self._libc: Any = None
        self._timesyncd = Path("/run/systemd/timesync/synchronized")
        try:
            libc = ctypes.CDLL("libc.so.6", use_errno=True)
            if libc.adjtimex(ctypes.create_string_buffer(512)) >= 0:
                self._libc = libc
        except (OSError, AttributeError):
            self._libc = None
        # ProtectClock=yes blocks the @clock syscall group, adjtimex included, so
        # the sandboxed service normally falls back to timesyncd's own flag.
        self.source = "adjtimex" if self._libc else "systemd-timesyncd"

    def synchronized(self) -> bool:
        if self._libc is not None:
            buf = ctypes.create_string_buffer(512)  # modes = 0 -> read-only query
            rc = self._libc.adjtimex(buf)
            return rc >= 0 and rc != self.TIME_ERROR
        return self._timesyncd.exists()

    def observe(self, system_ns: int, monotonic_ns: int) -> None:
        offset = system_ns - monotonic_ns
        if self._offset_ns is not None and abs(offset - self._offset_ns) > CLOCK_STEP_NS:
            self._last_step_mono_ns = monotonic_ns
            self._steps += 1
            log(
                f"host clock stepped by {(offset - self._offset_ns) / 1e6:.1f} ms; "
                f"latency withheld for {LATENCY_BLACKOUT_S:.0f} s"
            )
        self._offset_ns = offset

    def latency_valid(self, monotonic_ns: int) -> bool:
        if not self.synchronized():
            return False
        if self._last_step_mono_ns is None:
            return True
        return (monotonic_ns - self._last_step_mono_ns) > (LATENCY_BLACKOUT_S * 1e9)

    @property
    def steps(self) -> int:
        return self._steps


# --- Local store -------------------------------------------------------------

SCHEMA = """
CREATE TABLE IF NOT EXISTS sample (
    id           INTEGER PRIMARY KEY,
    node_id      INTEGER NOT NULL,
    subject_id   INTEGER NOT NULL,
    t_acq_us     INTEGER NOT NULL,
    t_rx_ns      INTEGER NOT NULL,
    t_rx_mono_ns INTEGER NOT NULL,
    t_store_ns   INTEGER NOT NULL,
    latency_ns   INTEGER,
    value        REAL,
    payload      TEXT
);
CREATE INDEX IF NOT EXISTS sample_by_store_time ON sample(t_store_ns);
CREATE INDEX IF NOT EXISTS sample_by_series ON sample(node_id, subject_id, t_rx_ns);

CREATE TABLE IF NOT EXISTS node_event (
    id         INTEGER PRIMARY KEY,
    node_id    INTEGER NOT NULL,
    kind       TEXT NOT NULL,
    t_acq_us   INTEGER,
    t_rx_ns    INTEGER NOT NULL,
    t_store_ns INTEGER NOT NULL,
    payload    TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS node_event_by_store_time ON node_event(t_store_ns);
"""


class Store:
    """The bounded local store (ADR-0020 d2), best-effort by decision (d3).

    WAL and `synchronous=NORMAL` are that decision expressed in pragmas: the
    store survives a reboot and a power cut loses at most the un-flushed tail,
    which is what keeps a dead gateway a drop-in swap rather than a data-recovery
    procedure. It is NOT a durability guarantee against device failure, and it is
    not the tamper-evident log ADR-0020 d9 refuses to resurrect.
    """

    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self._db = sqlite3.connect(str(path), isolation_level=None)
        self._db.execute("PRAGMA journal_mode=WAL")
        self._db.execute("PRAGMA synchronous=NORMAL")
        self._db.executescript(SCHEMA)
        self._samples: list = []
        self._events: list = []
        self._last_commit = time.monotonic()
        self.written = 0

    @property
    def pending(self) -> int:
        return len(self._samples) + len(self._events)

    def add_sample(self, row) -> None:
        self._samples.append(row)

    def add_event(self, row) -> None:
        self._events.append(row)

    def due(self) -> bool:
        if self.pending >= COMMIT_ROWS:
            return True
        return self.pending > 0 and (time.monotonic() - self._last_commit) >= COMMIT_INTERVAL_S

    def commit(self) -> None:
        """Write the batch. `t_store` is stamped HERE, at the actual write."""
        if not self.pending:
            self._last_commit = time.monotonic()
            return
        t_store = time.time_ns()
        samples = [(r[0], r[1], r[2], r[3], r[4], t_store, r[5], r[6], r[7]) for r in self._samples]
        events = [(r[0], r[1], r[2], r[3], t_store, r[4]) for r in self._events]
        try:
            self._db.execute("BEGIN")
            if samples:
                self._db.executemany(
                    "INSERT INTO sample (node_id, subject_id, t_acq_us, t_rx_ns, t_rx_mono_ns,"
                    " t_store_ns, latency_ns, value, payload) VALUES (?,?,?,?,?,?,?,?,?)",
                    samples,
                )
            if events:
                self._db.executemany(
                    "INSERT INTO node_event (node_id, kind, t_acq_us, t_rx_ns, t_store_ns, payload)"
                    " VALUES (?,?,?,?,?,?)",
                    events,
                )
            self._db.execute("COMMIT")
            self.written += len(samples) + len(events)
        except sqlite3.Error as exc:
            self._db.execute("ROLLBACK")
            log(f"store write FAILED, batch dropped: {exc!r}")
        self._samples.clear()
        self._events.clear()
        self._last_commit = time.monotonic()

    def evict(self) -> int:
        """Oldest-first eviction past the time bound (ADR-0020 d2)."""
        cutoff = time.time_ns() - int(RETENTION_DAYS * 86400e9)
        removed = (
            self._db.execute("DELETE FROM sample WHERE t_store_ns < ?", (cutoff,)).rowcount or 0
        )
        removed += (
            self._db.execute("DELETE FROM node_event WHERE t_store_ns < ?", (cutoff,)).rowcount or 0
        )
        return removed

    def close(self) -> None:
        self.commit()
        self._db.close()


# --- Live working set (ADR-0020 d6) ------------------------------------------


@dataclass
class NodeState:
    samples: int = 0
    events: int = 0
    latency_withheld: int = 0
    health: int | None = None
    mode: int | None = None
    latest: dict = field(default_factory=dict)  # subject_id -> value or payload


HEALTH_NAMES = {0: "NOMINAL", 1: "ADVISORY", 2: "CAUTION", 3: "WARNING"}


class Consumer:
    def __init__(self, store: Store | None) -> None:
        self.store = store
        self.clock = ClockGuard()
        self.nodes: dict = {}
        self._hb_last: dict = {}
        self._hb_last_stored: dict = {}

    def _node(self, node_id: int) -> NodeState:
        return self.nodes.setdefault(node_id, NodeState())

    def on_sample(self, subject: Any, msg: Any, meta: Any) -> None:
        node_id = meta.source_node_id
        if node_id is None:
            return  # an anonymous telemetry publisher is not a node we can key on
        ts = meta.timestamp
        self.clock.observe(ts.system_ns, ts.monotonic_ns)

        t_acq_us = int(msg.timestamp.microsecond)
        latency_ns: int | None = None
        if t_acq_us != 0 and self.clock.latency_valid(ts.monotonic_ns):
            latency_ns = ts.system_ns - (t_acq_us * 1000)

        value, payload = subject.extract(msg)
        state = self._node(node_id)
        state.samples += 1
        if t_acq_us != 0 and latency_ns is None:
            state.latency_withheld += 1
        state.latest[subject.subject_id] = value if value is not None else payload

        if self.store is not None:
            self.store.add_sample(
                (
                    node_id,
                    subject.subject_id,
                    t_acq_us,
                    ts.system_ns,
                    ts.monotonic_ns,
                    latency_ns,
                    value,
                    json.dumps(payload) if payload is not None else None,
                )
            )

    def on_heartbeat(self, msg: Any, meta: Any) -> None:
        node_id = meta.source_node_id
        if node_id is None:
            return
        state = self._node(node_id)
        state.health = int(msg.health.value)
        state.mode = int(msg.mode.value)
        key = (state.health, state.mode, int(msg.vendor_specific_status_code))
        now = time.monotonic()
        changed = self._hb_last.get(node_id) != key
        stale = (now - self._hb_last_stored.get(node_id, -1e9)) >= HEARTBEAT_KEEPALIVE_S
        self._hb_last[node_id] = key
        if not (changed or stale):
            return
        self._hb_last_stored[node_id] = now
        state.events += 1
        if self.store is not None:
            # Heartbeat carries no timestamp field, so t_acq is ABSENT, not zero.
            self.store.add_event(
                (
                    node_id,
                    "heartbeat",
                    None,
                    meta.timestamp.system_ns,
                    json.dumps(
                        {
                            "health": state.health,
                            "mode": state.mode,
                            "uptime_s": int(msg.uptime),
                            "reset_cause": int(msg.vendor_specific_status_code),
                            "reason": "change" if changed else "keepalive",
                        }
                    ),
                )
            )

    def on_diagnostic(self, msg: Any, meta: Any) -> None:
        node_id = meta.source_node_id
        if node_id is None:
            return
        state = self._node(node_id)
        state.events += 1
        text = bytes(msg.text).decode("utf-8", "replace")
        log(f"node {node_id} diagnostic sev={int(msg.severity.value)}: {text}")
        if self.store is not None:
            self.store.add_event(
                (
                    node_id,
                    "diagnostic",
                    int(msg.timestamp.microsecond),
                    meta.timestamp.system_ns,
                    json.dumps({"severity": int(msg.severity.value), "text": text}),
                )
            )

    def report(self) -> None:
        if not self.nodes:
            log(f"no nodes seen on {CAN_IFACE}")
            return
        for node_id in sorted(self.nodes):
            s = self.nodes[node_id]
            health = HEALTH_NAMES.get(s.health, "?") if s.health is not None else "no heartbeat"
            log(
                f"node {node_id}: {s.samples} samples, {s.events} events, health {health}, "
                f"{len(s.latest)} subjects live, {s.latency_withheld} latency withheld"
            )


# --- Wiring ------------------------------------------------------------------


async def run(seconds: float | None) -> int:
    try:
        import igrow_subjects
        import pycyphal.presentation
        import uavcan.diagnostic
        import uavcan.node
        from pycyphal.transport.can import CANTransport
        from pycyphal.transport.can.media.socketcan import SocketCANMedia
    except ImportError as exc:
        log(f"cannot start: {exc!r}")
        log(
            "PYTHONPATH must point at the namespaces compiled by provision.sh, "
            "and SocketCAN exists only on Linux"
        )
        return 1

    store = Store(DB_PATH) if STORE_ENABLED else None
    if store is None:
        log(
            "local store OFF (IGROW_PERSISTENT_BUFFER): t_store is absent and nothing is "
            "retained. Set it on for an SSD/NVMe medium -- ADR-0020 d2/d10"
        )
    else:
        log(f"store {DB_PATH}, retention {RETENTION_DAYS:g} days")

    consumer = Consumer(store)
    log(
        f"clock discipline read from {consumer.clock.source}; "
        f"latency blackout {LATENCY_BLACKOUT_S:.0f} s after a step"
    )

    try:
        media = SocketCANMedia(CAN_IFACE, mtu=8)  # classic CAN, ADR-0002 d8
        transport = CANTransport(media, local_node_id=None)  # subscribe-only, anonymous
        pres = pycyphal.presentation.Presentation(transport)
    except OSError as exc:
        # The ordinary case: the interface is not up. industrygrow-can.service
        # owns that, and a traceback would say less than its name does.
        log(f"cannot open {CAN_IFACE}: {exc!r}; is industrygrow-can.service up?")
        if store is not None:
            store.close()
        return 1

    subs = []
    try:
        for subject in igrow_subjects.SUBJECTS:
            sub = pres.make_subscriber(subject.dtype, subject.subject_id)
            sub.receive_in_background(lambda msg, meta, s=subject: consumer.on_sample(s, msg, meta))
            subs.append(sub)
        hb = pres.make_subscriber(uavcan.node.Heartbeat_1_0, HEARTBEAT_SUBJECT_ID)
        hb.receive_in_background(consumer.on_heartbeat)
        subs.append(hb)
        diag = pres.make_subscriber(uavcan.diagnostic.Record_1_1, DIAGNOSTIC_SUBJECT_ID)
        diag.receive_in_background(consumer.on_diagnostic)
        subs.append(diag)
        log(
            f"subscribed on {CAN_IFACE}: {len(igrow_subjects.SUBJECTS)} telemetry subjects "
            "+ heartbeat + diagnostic"
        )

        stop = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGTERM, signal.SIGINT):
            # Not every platform offers them on the loop; the unit only needs SIGTERM.
            with contextlib.suppress(NotImplementedError, RuntimeError):
                loop.add_signal_handler(sig, stop.set)
        if seconds is not None:
            loop.call_later(seconds, stop.set)

        next_report = time.monotonic() + REPORT_S
        next_sweep = time.monotonic() + RETENTION_SWEEP_S
        while not stop.is_set():
            # One second is the housekeeping tick, not a deadline: the timeout IS
            # the normal path, and stop.wait() returning early is the shutdown.
            with contextlib.suppress(TimeoutError):
                await asyncio.wait_for(stop.wait(), timeout=1.0)
            now = time.monotonic()
            if store is not None and store.due():
                store.commit()
            if now >= next_report:
                next_report = now + REPORT_S
                consumer.report()
            if store is not None and now >= next_sweep:
                next_sweep = now + RETENTION_SWEEP_S
                removed = store.evict()
                if removed:
                    log(f"evicted {removed} rows past the {RETENTION_DAYS:g}-day bound")
    finally:
        for sub in subs:
            sub.close()
        pres.close()
        if store is not None:
            store.close()
            log(f"store closed, {store.written} rows written this run")

    consumer.report()
    if consumer.clock.steps:
        log(f"{consumer.clock.steps} host clock step(s) seen this run")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="IndustryGrow gateway telemetry consumer")
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--serve", action="store_true", help="consume until stopped (default)")
    group.add_argument(
        "--once", type=float, metavar="SECONDS", help="consume for SECONDS, report, exit"
    )
    args = ap.parse_args()
    return asyncio.run(run(args.once))


if __name__ == "__main__":
    sys.exit(main())
