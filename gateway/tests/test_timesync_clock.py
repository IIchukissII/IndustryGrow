# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The time master's clock gate, without a bus (ADR-0002 d11).

The published base is the host's CLOCK_REALTIME, so a master that starts before
NTP converges hands every node a base that later steps. `wait_for_clock` is what
prevents that, and it is pure enough to test directly: it reads two paths and a
clock, and returns whether the base is trustworthy.

What matters is that it never becomes a way to NOT publish. A gateway with no NTP
client, and one whose NTP never converges, must both end up on the bus — the
first immediately, the second after the bound.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

GATEWAY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(GATEWAY / "files" / "app"))

import gateway_timesync as ts  # noqa: E402


def test_no_timesyncd_does_not_wait(tmp_path, monkeypatch):
    """A host that runs no systemd-timesyncd has nothing to wait for."""
    monkeypatch.setattr(ts, "TIMESYNC_DIR", str(tmp_path / "absent"))
    started = time.monotonic()
    assert ts.wait_for_clock(30.0) is True
    assert time.monotonic() - started < 1.0


def test_synchronized_returns_immediately(tmp_path, monkeypatch):
    (tmp_path / "synchronized").write_text("")
    monkeypatch.setattr(ts, "TIMESYNC_DIR", str(tmp_path))
    monkeypatch.setattr(ts, "TIMESYNC_STAMP", str(tmp_path / "synchronized"))
    started = time.monotonic()
    assert ts.wait_for_clock(30.0) is True
    assert time.monotonic() - started < 1.0


def test_unsynchronized_publishes_after_the_bound(tmp_path, monkeypatch):
    """The timeout is a warning, not a failure: never publishing is worse."""
    monkeypatch.setattr(ts, "TIMESYNC_DIR", str(tmp_path))
    monkeypatch.setattr(ts, "TIMESYNC_STAMP", str(tmp_path / "synchronized"))
    monkeypatch.setattr(ts, "CLOCK_POLL_S", 0.01)
    assert ts.wait_for_clock(0.05) is False


def test_zero_timeout_skips_the_gate(tmp_path, monkeypatch):
    """`--once` is a manual check and must not block on the operator's clock."""
    monkeypatch.setattr(ts, "TIMESYNC_DIR", str(tmp_path))
    monkeypatch.setattr(ts, "TIMESYNC_STAMP", str(tmp_path / "synchronized"))
    assert ts.wait_for_clock(0.0) is True


def test_step_threshold_is_below_one_publication_period():
    """A step must be detectable inside a single period, or it is missed."""
    assert ts.CLOCK_STEP_S < ts.PUBLICATION_PERIOD
