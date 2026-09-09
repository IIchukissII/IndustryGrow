#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Canopy capture for the P1 phase of the imaging rollout (project/ROADMAP.md).

P1 answers four questions and produces no record: what cadence is right, whether
the resolution shows what matters, what the frames actually cost to store, and
whether far-red reaches the sensor through the lens. Its frames carry no
reference surface and no fixed pose, so they are not comparable and are not the
series. What P1 does build is the habit P2 needs: nothing is automatic, and
whatever was applied is written down beside the frame.

Every capture pins exposure, gain, white balance and focus, then records what the
camera reported back, so a frame can never be quietly re-metered. The script
refuses to run until those values are set, which is the point.

Runs on a Raspberry Pi with a camera on the CSI interface. Needs the system
picamera2 (`sudo apt install -y python3-picamera2`), NOT the gateway venv, which
is PEP 668 isolated and has no access to it.

    python3 canopy_capture.py --init-config     # write the template, then edit it
    python3 canopy_capture.py --focus-sweep     # find the lens position, once
    python3 canopy_capture.py --probe           # find an exposure that does not clip
    python3 canopy_capture.py                   # one pinned capture: DNG + JPEG + JSON
    python3 canopy_capture.py --status          # frames held, bytes, projected cycle

Daily capture, mid-photoperiod so the luminaire is in a repeatable state:

    5 13 * * * /usr/bin/python3 /home/igrow/canopy_capture.py >> /home/igrow/cap.log 2>&1
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from datetime import UTC, datetime
from pathlib import Path

VERSION = "1"
DEFAULT_CONFIG = Path(__file__).with_name("canopy_capture.json")

# Values that must be determined on the bench before the series starts. A null
# here is not a default to fall back on; it is a refusal to capture.
CONFIG_TEMPLATE = {
    "output_dir": "~/canopy",
    "lens_position": None,
    "exposure_us": None,
    "analogue_gain": None,
    "colour_gains": None,
    "settle_seconds": 2.0,
    "jpeg_quality": 95,
    "light_state_command": None,
    "_notes": "lens_position in dioptres (0 = infinity); colour_gains as [red, blue].",
}
PINNED = ("lens_position", "exposure_us", "analogue_gain", "colour_gains")


def load_config(path: Path) -> dict:
    if not path.exists():
        sys.exit(f"no config at {path} — run --init-config first")
    cfg = json.loads(path.read_text())
    cfg["output_dir"] = Path(os.path.expanduser(cfg["output_dir"]))
    return cfg


def require_pinned(cfg: dict) -> None:
    missing = [k for k in PINNED if cfg.get(k) is None]
    if missing:
        sys.exit(
            "these are still unset: " + ", ".join(missing) + "\n"
            "Run --focus-sweep and --probe, write the values into the config, then capture."
        )


def clock_is_synced() -> bool | None:
    """A Pi without an RTC boots with a wrong clock, so the frame time is only
    as good as NTP. Recorded rather than assumed."""
    try:
        out = subprocess.run(
            ["timedatectl", "show", "-p", "NTPSynchronized", "--value"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        return out.stdout.strip() == "yes"
    except (OSError, subprocess.SubprocessError):
        return None


def open_camera(cfg: dict, want_raw: bool):
    from picamera2 import Picamera2

    cam = Picamera2()
    kwargs = {"main": {"size": cam.sensor_resolution}}
    if want_raw:
        kwargs["raw"] = {"size": cam.sensor_resolution}
    cam.configure(cam.create_still_configuration(**kwargs))
    cam.options["quality"] = cfg["jpeg_quality"]
    return cam


def pinned_controls(cfg: dict, cam) -> dict:
    """Everything automatic is switched off. Anything left on would move in the
    same direction as the thing being looked at."""
    ctrls = {
        "AeEnable": False,
        "AwbEnable": False,
        "ExposureTime": int(cfg["exposure_us"]),
        "AnalogueGain": float(cfg["analogue_gain"]),
        "ColourGains": tuple(float(g) for g in cfg["colour_gains"]),
    }
    for name, value in (
        ("Brightness", 0.0),
        ("Contrast", 1.0),
        ("Saturation", 1.0),
        ("Sharpness", 1.0),
    ):
        if name in cam.camera_controls:
            ctrls[name] = value
    if "NoiseReductionMode" in cam.camera_controls:
        ctrls["NoiseReductionMode"] = 0
    if "AfMode" in cam.camera_controls:
        # Module 3 refocuses on its own otherwise, and the lens model, the
        # distortion coefficients and the registration all move with it.
        ctrls["AfMode"] = 0
        ctrls["LensPosition"] = float(cfg["lens_position"])
    return ctrls


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def light_state(cfg: dict) -> dict | None:
    """Decision 7 records the light at the capture instant beside the frame. P1
    has no bus client, so this is whatever command the operator configures."""
    cmd = cfg.get("light_state_command")
    if not cmd:
        return None
    try:
        out = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=30)
        return {
            "command": cmd,
            "returncode": out.returncode,
            "stdout": out.stdout.strip()[:4000],
            "stderr": out.stderr.strip()[:1000],
        }
    except (OSError, subprocess.SubprocessError) as exc:
        return {"command": cmd, "error": str(exc)}


def green_plane(array):
    import numpy as np

    a = np.asarray(array)
    return a[..., 1].astype(np.float32) if a.ndim == 3 else a.astype(np.float32)


def capture(cfg: dict) -> int:
    require_pinned(cfg)
    out = cfg["output_dir"]
    out.mkdir(parents=True, exist_ok=True)

    cam = open_camera(cfg, want_raw=True)
    ctrls = pinned_controls(cfg, cam)
    cam.set_controls(ctrls)
    cam.start()
    time.sleep(cfg["settle_seconds"])

    stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%SZ")
    dng, jpg, side = out / f"{stamp}.dng", out / f"{stamp}.jpg", out / f"{stamp}.json"

    request = cam.capture_request()
    try:
        request.save("main", str(jpg))
        request.save_dng(str(dng))
        applied = dict(request.get_metadata())
    finally:
        request.release()
        cam.stop()
        cam.close()

    record = {
        "script": Path(__file__).name,
        "script_version": VERSION,
        "captured_utc": stamp,
        "clock_ntp_synced": clock_is_synced(),
        "phase": "P1",
        "requested_controls": {k: list(v) if isinstance(v, tuple) else v for k, v in ctrls.items()},
        "applied_metadata": {
            k: (list(v) if isinstance(v, (tuple, list)) else v)
            for k, v in applied.items()
            if isinstance(v, (int, float, str, tuple, list, bool))
        },
        "light_state": light_state(cfg),
        "files": {f.name: {"bytes": f.stat().st_size, "sha256": sha256(f)} for f in (dng, jpg)},
    }
    side.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")

    exp = applied.get("ExposureTime")
    gain = applied.get("AnalogueGain")
    print(
        f"{stamp}  dng {dng.stat().st_size / 1e6:.1f} MB  "
        f"exposure {exp} us  gain {gain}  synced {record['clock_ntp_synced']}"
    )
    pinned_us = int(cfg["exposure_us"])
    if exp is not None and abs(exp - pinned_us) > max(50, pinned_us * 0.02):
        print("  WARNING: applied exposure differs from the pinned value", file=sys.stderr)
    return 0


def probe(cfg: dict) -> int:
    """Report how close the pinned exposure runs to clipping, per colour plane.

    Read off the processed image, which carries a tone curve, so this is
    conservative: it calls clipping sooner than the raw data would.
    """
    import numpy as np

    if cfg.get("exposure_us") is None or cfg.get("analogue_gain") is None:
        sys.exit("set exposure_us and analogue_gain to the values you want to test first")
    cfg = dict(
        cfg,
        colour_gains=cfg.get("colour_gains") or [2.0, 2.0],
        lens_position=cfg.get("lens_position") or 0.0,
    )

    cam = open_camera(cfg, want_raw=False)
    cam.set_controls(pinned_controls(cfg, cam))
    cam.start()
    time.sleep(cfg["settle_seconds"])
    arr = np.asarray(cam.capture_array("main"))
    cam.stop()
    cam.close()

    print(f"exposure {cfg['exposure_us']} us, gain {cfg['analogue_gain']}")
    for i, name in enumerate(("red", "green", "blue")[: arr.shape[-1]]):
        plane = arr[..., i]
        clipped = float((plane >= 254).mean()) * 100.0
        print(
            f"  {name:<6} p99.9 {np.percentile(plane, 99.9):6.1f}   "
            f"max {plane.max():4d}   clipped {clipped:6.3f} %"
        )
    print("Aim for no plane above about 0.01 % clipped with headroom as the canopy fills in.")
    return 0


def focus_sweep(cfg: dict, lo: float, hi: float, steps: int) -> int:
    """Capture across lens positions and score each one, so the position can be
    chosen once and then pinned. Intrinsics move with focus, so this is a
    precondition for calibration and not a preference."""
    import numpy as np

    cam = open_camera(cfg, want_raw=False)
    if "AfMode" not in cam.camera_controls:
        cam.close()
        sys.exit("this camera has no focuser — set lens_position to 0 and skip this step")

    out = cfg["output_dir"] / "focus-sweep"
    out.mkdir(parents=True, exist_ok=True)
    base = dict(
        cfg,
        colour_gains=cfg.get("colour_gains") or [2.0, 2.0],
        exposure_us=cfg.get("exposure_us") or 20000,
        analogue_gain=cfg.get("analogue_gain") or 1.0,
    )
    cam.start()

    best = None
    for position in [lo + (hi - lo) * i / (steps - 1) for i in range(steps)]:
        cam.set_controls(pinned_controls(dict(base, lens_position=position), cam))
        time.sleep(base["settle_seconds"])
        g = green_plane(cam.capture_array("main"))
        score = float(np.var(np.diff(g, axis=0)) + np.var(np.diff(g, axis=1)))
        cam.capture_file(str(out / f"lens{position:05.2f}.jpg"))
        print(f"  lens {position:5.2f}   sharpness {score:12.1f}")
        if best is None or score > best[1]:
            best = (position, score)
    cam.stop()
    cam.close()

    print(f"\nsharpest at lens_position {best[0]:.2f} — check {out} by eye before pinning it")
    return 0


def status(cfg: dict) -> int:
    out = cfg["output_dir"]
    frames = sorted(out.glob("*.dng")) if out.exists() else []
    if not frames:
        print(f"no frames in {out}")
        return 0
    total = sum(f.stat().st_size for f in frames)
    per = total / len(frames)
    print(f"{len(frames)} frames in {out}")
    print(f"  held        {total / 1e9:.2f} GB")
    print(f"  per frame   {per / 1e6:.1f} MB")
    print(f"  first       {frames[0].stem}")
    print(f"  last        {frames[-1].stem}")
    print(f"  at 1/day    {per * 90 / 1e9:.2f} GB per 90-day cycle")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    ap.add_argument("--init-config", action="store_true", help="write the config template")
    ap.add_argument("--probe", action="store_true", help="report clipping at the pinned exposure")
    ap.add_argument("--focus-sweep", action="store_true", help="score lens positions")
    ap.add_argument("--status", action="store_true", help="frames held and projected volume")
    ap.add_argument("--lens-range", nargs=2, type=float, default=(0.0, 10.0), metavar=("LO", "HI"))
    ap.add_argument("--lens-steps", type=int, default=21)
    args = ap.parse_args()

    if args.init_config:
        if args.config.exists():
            sys.exit(f"{args.config} exists already — edit it rather than overwriting it")
        args.config.write_text(json.dumps(CONFIG_TEMPLATE, indent=2) + "\n")
        print(f"wrote {args.config} — set {', '.join(PINNED)} before capturing")
        return 0

    cfg = load_config(args.config)
    if args.status:
        return status(cfg)
    if args.focus_sweep:
        return focus_sweep(cfg, args.lens_range[0], args.lens_range[1], args.lens_steps)
    if args.probe:
        return probe(cfg)

    lock = cfg["output_dir"] / ".capture.lock"
    cfg["output_dir"].mkdir(parents=True, exist_ok=True)
    try:
        fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        sys.exit(f"another capture holds {lock} — remove it if no capture is running")
    try:
        os.write(fd, str(os.getpid()).encode())
        os.close(fd)
        return capture(cfg)
    finally:
        lock.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
