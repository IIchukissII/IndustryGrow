#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Gateway firmware client — the transfer ADR-0029 decisions 14-17 describe.

The ERP records which release an operator intends for this machine (ADR-0022
d14) and nothing consumed it. This is that consumer, and it is the whole of
decision 16's loop: pull the intent, hold the artifacts on disk, ask each node
what it is running, and update the ones that are not running it.

    firmware_client.py once    # one pass over the bus; what the timer runs
    firmware_client.py show    # the intent, the artifacts held, and what is running
    firmware_client.py serve   # poll on an interval, for a host without timers

WHAT THIS DOES NOT DO. It reports nothing back to the ERP. There is no route to
report to and that is deliberate (ADR-0022 d9, alternative Q): what a node runs
is operational, it is observed here, and it stays here. The ERP holds the
operator's intent; the journal below holds what came of it.

THE SLOT IS THE HARD PART (ADR-0029 d17). A release ships one image per slot
because the application is not position-independent, the *node* picks the slot it
writes -- the one it is not running -- and nothing on the bus names that slot.
So the running slot is deduced from the image itself: each `.img` header carries
a CRC over its own body, `GetInfo` reports that CRC for the running image, and
matching the two against the artifacts held on disk names the release and the
slot in one comparison. A node whose image matches nothing held is left alone and
reported, never guessed at -- see `plan_for_node`.

WHOEVER SENDS THE COMMAND SERVES THE FILE. The bootloader reads the artifact back
from the node that sent `COMMAND_BEGIN_SOFTWARE_UPDATE`, so this process is a
`uavcan.file.Read` server for as long as the transfer lasts.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import os
import ssl
import struct
import sys
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

from profile_client import CONFIG_DIR, PKI_DIR, ProfileError, machine_identity

# ADR-0029 d14: the gateway holds the artifacts it serves. Under /var/lib rather
# than /etc because these are data the gateway can re-fetch, not configuration an
# operator edits -- and ADR-0020 d5 enumerates what may persist on a replaceable
# edge host, where a re-fetchable cache is the cheapest possible entry.
ARTIFACT_DIR = Path(os.environ.get("IGROW_FIRMWARE_DIR", "/var/lib/industrygrow/firmware"))

DEFAULT_INTERVAL = 3600
DEFAULT_TIMEOUT = 60

CAN_IFACE = os.environ.get("IGROW_CAN_IFACE") or "vcan0"

# This process's own Node-ID while it runs. Distinct from the time master's
# (ADR-0002 d11) and from the provisioning tool's, so a firmware pass and a
# provisioning session cannot collide on the bus.
SERVER_NODE_ID = int(os.environ.get("IGROW_FIRMWARE_NODE_ID") or "3")

# uavcan.node.ExecuteCommand.COMMAND_BEGIN_SOFTWARE_UPDATE (ADR-0029 d5).
COMMAND_BEGIN_SOFTWARE_UPDATE = 65533

# uavcan.file.Read returns at most 256 bytes and the bootloader asks in that unit
# (firmware/boot/update.c). Matching it keeps every write offset word-aligned,
# which is what the node's flash programmer requires.
READ_BLOCK = 256

# How long one node's transfer may take before this gives up on it. The
# bootloader's own whole-transfer bound is 300 s; this is that plus the restart
# and bring-up either side of it.
TRANSFER_TIMEOUT_S = float(os.environ.get("IGROW_FIRMWARE_TRANSFER_TIMEOUT_S") or "420")

SURVEY_S = 6.0
GETINFO_TIMEOUT_S = 3.0

# --- the image header (firmware/common/platform/image.h) ---------------------
# The layout is an implementation specification (ADR-0029 d7) and image.h is its
# statement; mkimage.py writes it and this reads it. Only the fields needed to
# identify an image are read -- the signature is the node's business, not this
# process's, and checking it here would be a second opinion nobody acts on.

IMAGE_MAGIC = 0x4947494D  # 'IGIM'
IMAGE_HEADER_SIZE = 512
_HEADER_PREFIX = struct.Struct("<IIII HH I Q I")  # magic .. body_crc32, through 0x24


class FirmwareError(Exception):
    """A refusal. Nothing is written to a node and the pass moves on."""


def log(message: str) -> None:
    print(f"[firmware-client] {message}", flush=True)


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Config:
    base: str
    chain: Path
    anchor: Path
    timeout: int = DEFAULT_TIMEOUT
    interval: int = DEFAULT_INTERVAL

    @classmethod
    def from_env(cls) -> Config:
        url = os.environ.get("IGROW_ERP_URL", "").rstrip("/")
        if not url:
            raise FirmwareError(
                "IGROW_ERP_URL is not set — there is no ERP to pull an intended release\n"
                f"       from. Set it in {CONFIG_DIR}/gateway.env."
            )
        return cls(
            base=f"{url}/api/v1/gateway/firmware",
            chain=Path(os.environ.get("IGROW_GATEWAY_CHAIN", PKI_DIR / "gateway-chain.crt")),
            anchor=Path(os.environ.get("IGROW_OPERATOR_ROOT", PKI_DIR / "operator-root.crt")),
            timeout=int(os.environ.get("IGROW_FIRMWARE_TIMEOUT", DEFAULT_TIMEOUT)),
            interval=int(os.environ.get("IGROW_FIRMWARE_INTERVAL", DEFAULT_INTERVAL)),
        )


# ---------------------------------------------------------------------------
# The ERP channel — the same mTLS shape the profile pull uses
# ---------------------------------------------------------------------------


def _context(config: Config) -> ssl.SSLContext:
    for path, what in ((config.chain, "client chain"), (config.anchor, "operator root")):
        if not path.exists():
            raise FirmwareError(f"no {what} at {path}")
    context = ssl.create_default_context(cafile=str(config.anchor))
    context.load_cert_chain(str(config.chain), str(config.chain))
    return context


def _get(config: Config, url: str) -> bytes:
    try:
        with urllib.request.urlopen(url, timeout=config.timeout, context=_context(config)) as reply:
            return reply.read()
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            raise FirmwareError(
                "the ERP has no firmware release recorded for this machine — an operator "
                "selects one in the console"
            ) from exc
        if exc.code in (401, 403):
            raise FirmwareError(
                f"the ERP rejected this gateway's certificate (HTTP {exc.code})"
            ) from exc
        if exc.code == 503:
            raise FirmwareError(
                "the ERP's gateway channel is closed (HTTP 503) — its trusted-proxy list is "
                "empty, so no client certificate would be accepted (ADR-0022 d2)."
            ) from exc
        raise FirmwareError(f"the ERP returned HTTP {exc.code} for {url}") from exc
    except (urllib.error.URLError, ssl.SSLError, OSError) as exc:
        raise FirmwareError(f"cannot reach the ERP at {url}: {exc}") from exc


def pull_intent(config: Config) -> dict:
    """The release this machine is meant to run, and the artifacts it is made of."""
    import json

    body = _get(config, config.base)
    try:
        intent = json.loads(body)
    except json.JSONDecodeError as exc:
        raise FirmwareError(f"the ERP's reply is not JSON: {exc}") from exc
    if not isinstance(intent, dict) or not intent.get("release_root"):
        raise FirmwareError("the ERP's reply carries no release_root")
    return intent


def fetch_artifacts(config: Config, intent: dict) -> list[Path]:
    """Bring every artifact of the intended release onto disk; return their paths.

    Already-held artifacts are not re-fetched. An object key is immutable in the
    warehouse and a release is published once, so a file that is present under its
    key is the file — and a node about to be flashed is a poor moment to discover
    the network is down.
    """
    ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
    paths = []
    for key in intent.get("artifact_keys") or []:
        # The key comes from the ERP over an authenticated channel, but it still
        # names a file this process creates, so it is taken as a bare name and
        # never as a path: a key carrying a separator would otherwise write
        # outside the artifact directory.
        target = ARTIFACT_DIR / Path(key).name
        if target.exists() and target.stat().st_size > IMAGE_HEADER_SIZE:
            paths.append(target)
            continue
        log(f"fetching {key}")
        blob = _get(config, f"{config.base}/{key}/content")
        fd, staged = tempfile.mkstemp(dir=ARTIFACT_DIR, suffix=".part")
        try:
            os.write(fd, blob)
            os.fsync(fd)
            os.close(fd)
            os.chmod(staged, 0o644)
            os.replace(staged, target)
        except BaseException:
            with contextlib.suppress(OSError):
                os.close(fd)
            Path(staged).unlink(missing_ok=True)
            raise
        paths.append(target)
    if not paths:
        raise FirmwareError("the intended release names no artifacts")
    return paths


# ---------------------------------------------------------------------------
# Images held on disk
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Image:
    """One `.img` on disk, read into what identifies it."""

    path: Path
    release_root: str  # Exxxx-VVVVVV-F
    slot: str  # "slot-a" | "slot-b"
    version: tuple[int, int]
    body_crc32: int
    hardware_class: int

    @property
    def label(self) -> str:
        return f"{self.release_root} {self.slot} v{self.version[0]}.{self.version[1]}"


def read_image(path: Path) -> Image | None:
    """Read an artifact's header, or None when it is not one of ours.

    None rather than an exception: the artifact directory is a cache this process
    fills, and a truncated download or a file left by something else should be
    skipped, not turned into a failure of the whole pass.
    """
    name = path.name
    if not name.endswith(".img"):
        return None
    stem = name[: -len(".img")]
    release_root, sep, slot = stem.rpartition("-slot-")
    if not sep or slot not in ("a", "b"):
        return None
    try:
        header = path.read_bytes()[:IMAGE_HEADER_SIZE]
    except OSError:
        return None
    if len(header) < IMAGE_HEADER_SIZE:
        return None
    magic, _hv, _len, hw_class, major, minor, _r0, _vcs, crc = _HEADER_PREFIX.unpack_from(header)
    if magic != IMAGE_MAGIC:
        return None
    return Image(
        path=path,
        release_root=release_root,
        slot=f"slot-{slot}",
        version=(major, minor),
        body_crc32=crc,
        hardware_class=hw_class,
    )


def catalogue() -> list[Image]:
    """Every image this gateway holds — the evidence d17's identification uses."""
    if not ARTIFACT_DIR.is_dir():
        return []
    found = (read_image(p) for p in sorted(ARTIFACT_DIR.iterdir()) if p.is_file())
    return [image for image in found if image is not None]


def other_slot(slot: str) -> str:
    return "slot-b" if slot == "slot-a" else "slot-a"


# ---------------------------------------------------------------------------
# What to do about one node (ADR-0029 d15, d17)
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Plan:
    node_id: int
    action: str  # "up-to-date" | "update" | "unidentified" | "no-image"
    reason: str
    serve: Image | None = None


def plan_for_node(node_id: int, running_crc: int | None, held: list[Image], release: str) -> Plan:
    """Decide what this node needs, from what it reports and what is held.

    The whole of d15 and d17 in one function, and deliberately pure so the
    decision can be tested without a bus.
    """
    intended = {image.slot: image for image in held if image.release_root == release}
    if len(intended) < 2:
        return Plan(node_id, "no-image", f"{release} is not fully held on disk")

    if running_crc is None:
        return Plan(
            node_id,
            "unidentified",
            "the node reports no software_image_crc, so what it runs cannot be established "
            "(ADR-0029 d15). Firmware older than the image header does this.",
        )

    if any(image.body_crc32 == running_crc for image in intended.values()):
        return Plan(node_id, "up-to-date", f"already running {release}")

    running = next((image for image in held if image.body_crc32 == running_crc), None)
    if running is None:
        return Plan(
            node_id,
            "unidentified",
            f"running image CRC 0x{running_crc:08X} matches nothing this gateway holds, so "
            f"the slot it occupies is unknown and the artifact for the other slot cannot be "
            f"chosen (ADR-0029 d17). Flash it over SWD, or make its release available here.",
        )

    target = other_slot(running.slot)
    return Plan(
        node_id,
        "update",
        f"running {running.label}; writing {target}",
        serve=intended[target],
    )


# ---------------------------------------------------------------------------
# The bus
# ---------------------------------------------------------------------------


class Bus:
    """Presentation session for one pass, with a file server attached.

    Its own Node-ID (`SERVER_NODE_ID`) rather than the provisioning tool's: the
    bootloader reads the artifact back from whichever node commanded the update,
    so this process must be addressable for the whole transfer, and two tools
    sharing an ID would take each other's responses.
    """

    def __init__(self) -> None:
        import pycyphal.presentation
        from pycyphal.transport.can import CANTransport
        from pycyphal.transport.can.media.socketcan import SocketCANMedia

        media = SocketCANMedia(CAN_IFACE, mtu=8)  # classic CAN, ADR-0002 d8
        self._transport = CANTransport(media, local_node_id=SERVER_NODE_ID)
        self.pres = pycyphal.presentation.Presentation(self._transport)
        self._served: Path | None = None
        self._server = None

    def close(self) -> None:
        self.pres.close()

    async def survey(self, seconds: float) -> set[int]:
        """Node-IDs heard on the bus. Heartbeats, as `provision_node_id` does."""
        import uavcan.node

        seen: set[int] = set()
        sub = self.pres.make_subscriber(uavcan.node.Heartbeat_1_0, 7509)
        try:
            loop = asyncio.get_running_loop()
            deadline = loop.time() + seconds
            while loop.time() < deadline:
                got = await sub.receive_for(max(deadline - loop.time(), 0.0))
                if got is None:
                    continue
                _msg, meta = got
                if meta.source_node_id is not None:
                    seen.add(meta.source_node_id)
        finally:
            sub.close()
        return seen

    async def get_info(self, node_id: int):
        import uavcan.node
        from provision_node_id import call_with_retry

        client = self.pres.make_client(uavcan.node.GetInfo_1_0, 430, node_id)
        client.response_timeout = GETINFO_TIMEOUT_S
        try:
            got = await call_with_retry(client, uavcan.node.GetInfo_1_0.Request())
        finally:
            client.close()
        return None if got is None else got[0]

    def serve_file(self, image: Image) -> None:
        """Answer `uavcan.file.Read` for `image`, whatever path is asked for.

        Whatever path: the bootloader echoes back the path this process put in the
        command, so there is exactly one file in flight and matching on the name
        would only add a way for the transfer to fail. Serving one image at a time
        is also what keeps this from being a general file server on the bus.
        """
        import uavcan.file
        import uavcan.primitive

        self._served = image.path
        blob = image.path.read_bytes()

        async def handler(request, _metadata):
            offset = int(request.offset)
            chunk = blob[offset : offset + READ_BLOCK] if offset < len(blob) else b""
            return uavcan.file.Read_1_1.Response(
                error=uavcan.file.Error_1_0(value=0),
                data=uavcan.primitive.Unstructured_1_0(value=bytearray(chunk)),
            )

        self._server = self.pres.get_server(uavcan.file.Read_1_1, 408)
        self._server.serve_in_background(handler)

    def stop_serving(self) -> None:
        if self._server is not None:
            self._server.close()
            self._server = None
        self._served = None

    async def begin_update(self, node_id: int, path: str) -> bool:
        """Send `COMMAND_BEGIN_SOFTWARE_UPDATE`; True when the node accepted it.

        The node restarts into its bootloader as soon as it has answered, so a
        lost response is indistinguishable from a refusal from here — which is why
        the caller confirms on the bus rather than on this return value.
        """
        import uavcan.node
        from provision_node_id import call_with_retry

        client = self.pres.make_client(uavcan.node.ExecuteCommand_1_0, 435, node_id)
        client.response_timeout = GETINFO_TIMEOUT_S
        try:
            got = await call_with_retry(
                client,
                uavcan.node.ExecuteCommand_1_0.Request(
                    command=COMMAND_BEGIN_SOFTWARE_UPDATE,
                    parameter=path.encode("ascii"),
                ),
            )
        finally:
            client.close()
        return got is not None and int(got[0].status) == 0


def image_crc_of(info) -> int | None:
    """The running image's CRC from a `GetInfo` response (ADR-0029 d15).

    The field is an optional array — a node with no image header reports it
    empty, and that is a real state (`plan_for_node` refuses on it) rather than a
    zero to compare against.
    """
    if info is None:
        return None
    crc = getattr(info, "software_image_crc", None)
    if crc is None or len(crc) < 1:
        return None
    return int(crc[0])


# ---------------------------------------------------------------------------
# One pass
# ---------------------------------------------------------------------------


async def update_node(bus: Bus, plan: Plan) -> bool:
    """Command one node and serve its transfer. True when it came back running it."""
    assert plan.serve is not None
    # The path is what the node echoes into uavcan.file.Read. The object key is
    # used verbatim: it is what the release is called everywhere else, so a
    # journal line here and an artifact in store/ name the same thing.
    path = plan.serve.path.name

    bus.serve_file(plan.serve)
    try:
        if not await bus.begin_update(plan.node_id, path):
            log(f"node {plan.node_id}: did not accept the update command")
            return False
        log(f"node {plan.node_id}: serving {path} ({plan.serve.path.stat().st_size} bytes)")

        # The node restarts, downloads, verifies and comes back. What settles it
        # is the bus, not the acknowledgement: watch for the node to reappear
        # running the image that was served.
        deadline = asyncio.get_running_loop().time() + TRANSFER_TIMEOUT_S
        while asyncio.get_running_loop().time() < deadline:
            await asyncio.sleep(10.0)
            info = await bus.get_info(plan.node_id)
            if info is None:
                continue  # in the bootloader, or mid-restart
            crc = image_crc_of(info)
            if crc == plan.serve.body_crc32:
                log(f"node {plan.node_id}: now running {plan.serve.label}")
                return True
        log(
            f"node {plan.node_id}: did not come back running {plan.serve.label} within "
            f"{TRANSFER_TIMEOUT_S:g}s. If the image failed to verify the node reverted to "
            f"what it had (ADR-0029 d6, d8) — check its diagnostic channel."
        )
        return False
    finally:
        bus.stop_serving()


async def pass_once(config: Config, dry_run: bool = False) -> int:
    """Pull the intent, then bring every node it applies to onto that release."""
    machine = machine_identity(config.chain)
    intent = pull_intent(config)
    release = intent["release_root"]
    fetch_artifacts(config, intent)
    held = catalogue()
    log(f"{machine}: intended release {release}; {len(held)} image(s) held")

    bus = Bus()
    try:
        nodes = await bus.survey(SURVEY_S)
        if not nodes:
            log(f"no nodes heard on {CAN_IFACE} in {SURVEY_S:g}s")
            return 1

        plans = []
        for node_id in sorted(nodes):
            info = await bus.get_info(node_id)
            plans.append(plan_for_node(node_id, image_crc_of(info), held, release))

        for plan in plans:
            log(f"node {plan.node_id}: {plan.action} — {plan.reason}")

        blocked = [p for p in plans if p.action in ("unidentified", "no-image")]
        todo = [p for p in plans if p.action == "update"]
        if dry_run:
            return 1 if blocked else 0

        updated = 0
        for plan in todo:
            # One node at a time. A transfer is a gateway-paced burst on a 500
            # kbit/s bus (ADR-0002 rev 3) and two at once would contend for it;
            # fleet-wide ordering and concurrency are ADR-0029's deferred item, so
            # this does the one thing that cannot contradict whatever it decides.
            if await update_node(bus, plan):
                updated += 1
        log(f"{updated}/{len(todo)} updated, {len(blocked)} needing an operator")
        return 1 if (blocked or updated != len(todo)) else 0
    finally:
        bus.close()


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------


def cmd_once(args: argparse.Namespace) -> int:
    config = Config.from_env()
    try:
        return asyncio.run(pass_once(config, dry_run=args.dry_run))
    except FirmwareError as exc:
        log(f"pass failed: {exc}")
        return 1
    except OSError as exc:
        log(f"cannot open {CAN_IFACE}: {exc!r}; is industrygrow-can.service up?")
        return 1


def cmd_show(args: argparse.Namespace) -> int:
    config = Config.from_env()
    try:
        log(f"machine: {machine_identity(config.chain)}")
        intent = pull_intent(config)
        log(f"intended release: {intent['release_root']}")
    except (FirmwareError, ProfileError) as exc:
        log(f"intent unavailable: {exc}")
    held = catalogue()
    if not held:
        log(f"no artifacts held in {ARTIFACT_DIR}")
    for image in held:
        log(f"held: {image.label}  crc=0x{image.body_crc32:08X}")
    return 0


def cmd_serve(args: argparse.Namespace) -> int:
    """Poll on an interval. The systemd timer is the production path."""
    config = Config.from_env()
    delay = config.interval
    ceiling = max(config.interval, args.max_interval)
    while True:
        try:
            asyncio.run(pass_once(config))
            delay = config.interval
        except KeyboardInterrupt:
            return 0
        except (FirmwareError, OSError) as exc:
            # OSError as well as a refusal: the CAN interface can be down or go
            # away mid-pass, and a loop that runs on a timer must outlive that
            # rather than exit and leave the machine with nothing polling.
            log(f"pass failed: {exc}")
            delay = min(delay * 2, ceiling)
            log(f"next attempt in {delay}s")
        time.sleep(delay)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="firmware_client.py",
        description="Bring this machine's nodes onto the firmware release the ERP records "
        "(ADR-0029 d14-17).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("once", help="one pass over the bus; what the timer runs")
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="report what each node needs and change nothing",
    )
    p.set_defaults(func=cmd_once)

    p = sub.add_parser("show", help="the intent and the artifacts held")
    p.set_defaults(func=cmd_show)

    p = sub.add_parser("serve", help="poll on an interval, for a host without timers")
    p.add_argument("--max-interval", type=int, default=21600, help="backoff ceiling in seconds")
    p.set_defaults(func=cmd_serve)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (FirmwareError, ProfileError) as exc:
        log(f"error: {exc}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
