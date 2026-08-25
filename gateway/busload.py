#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Exact Cyphal/CAN bus load, measured from the wire.

Counts real bits, not an estimate: for every frame it rebuilds the extended-frame
bit sequence, computes the CAN CRC-15 over it, applies the stuffing rule, and adds
the 13 fixed-form bits (CRC delimiter, ACK slot, ACK delimiter, EOF, IFS). Nothing
here is a worst-case bound, so the result can be compared against the headroom
ADR-0002 decision 8 claims for the 500 kbit/s rate.

Run on the gateway, by hand, against a live bus:

    python3 busload.py [seconds]        # default 60

Reports total load, load per source Node-ID, and load per subject. Interpreting
it: one 1 Hz two-frame telemetry subject costs about 0.054 % at 500 kbit/s.
"""
import socket, struct, sys, time
from collections import defaultdict

FMT="=IB3x8s"; SZ=struct.calcsize(FMT)
BITRATE=500_000

def crc15(bits):
    crc=0
    for b in bits:
        inv = b ^ ((crc>>14)&1)
        crc=(crc<<1)&0x7FFF
        if inv: crc ^= 0x4599
    return crc

def frame_bits(can_id, data):
    """Exact bit count for one extended data frame, stuffing included."""
    b=[0]                                            # SOF
    b += [(can_id>>i)&1 for i in range(28,17,-1)]    # ID[28:18]
    b += [1,1]                                       # SRR, IDE
    b += [(can_id>>i)&1 for i in range(17,-1,-1)]    # ID[17:0]
    b += [0,0,0]                                     # RTR, r1, r0
    n=len(data)
    b += [(n>>i)&1 for i in range(3,-1,-1)]          # DLC
    for byte in data:
        b += [(byte>>i)&1 for i in range(7,-1,-1)]
    c=crc15(b)
    b += [(c>>i)&1 for i in range(14,-1,-1)]         # CRC sequence
    # stuffing applies from SOF through the CRC sequence
    stuffed=0; run=1
    for i in range(1,len(b)):
        if b[i]==b[i-1]:
            run+=1
            if run==5:
                stuffed+=1; run=1                    # inserted opposite bit restarts the run
        else:
            run=1
    return len(b)+stuffed+13                         # +CRC delim, ACK, ACK delim, EOF(7), IFS(3)

window=float(sys.argv[1]) if len(sys.argv)>1 else 60.0
s=socket.socket(socket.AF_CAN,socket.SOCK_RAW,socket.CAN_RAW)
s.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,1<<20)
s.bind(("can0",)); s.settimeout(2.0)

bits_by_src=defaultdict(int); frames_by_src=defaultdict(int)
bits_by_subj=defaultdict(int); frames_by_subj=defaultdict(int)
total_bits=0; total_frames=0
t0=time.monotonic(); end=t0+window
while time.monotonic()<end:
    try: d=s.recv(SZ)
    except OSError: continue
    cid,dlc,payload=struct.unpack(FMT,d)
    eff=cid & 0x1FFFFFFF
    nb=frame_bits(eff, payload[:dlc])
    src=eff & 0x7F
    subj=(eff>>8)&0x1FFF if not (eff & (1<<25)) else -1
    total_bits+=nb; total_frames+=1
    bits_by_src[src]+=nb; frames_by_src[src]+=1
    bits_by_subj[subj]+=nb; frames_by_subj[subj]+=1
elapsed=time.monotonic()-t0

print(f"window {elapsed:.1f} s   frames {total_frames}   bits {total_bits}")
print(f"BUS LOAD: {total_bits/elapsed/BITRATE*100:.3f} %  of {BITRATE//1000} kbit/s"
      f"   ({total_bits/elapsed/1000:.2f} kbit/s, {total_frames/elapsed:.1f} frames/s)")
print("\nby source node:")
for src in sorted(bits_by_src):
    b=bits_by_src[src]
    print(f"  node {src:3d}: {frames_by_src[src]/elapsed:6.1f} fps  "
          f"{b/elapsed/1000:7.2f} kbit/s  {b/elapsed/BITRATE*100:6.3f} %")
print("\nby subject (top 12):")
for subj in sorted(bits_by_subj, key=lambda k:-bits_by_subj[k])[:12]:
    b=bits_by_subj[subj]
    name={7168:"time sync",7509:"heartbeat",7510:"port.List",8184:"diagnostic",-1:"service"}.get(subj,"")
    print(f"  subj {subj:5d} {name:11s}: {frames_by_subj[subj]/elapsed:6.1f} fps  {b/elapsed/BITRATE*100:6.3f} %")
