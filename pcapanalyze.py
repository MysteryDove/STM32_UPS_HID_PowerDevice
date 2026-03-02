#!/usr/bin/env python3
"""
Extract USB serial payload bytes from a Wireshark capture and print:
  Send: ASCII (HEX)
  Receive: ASCII (HEX)

Default display filter:
  (usb.data_len > 0) && (usbcom || usb.capdata)
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from typing import Iterable

DEFAULT_FILTER = "(usb.data_len > 0) && (usbcom || usb.capdata)"
CORE_FIELDS = [
    "frame.number",
    "usb.endpoint_address.direction",
    "usb.src",
    "usb.dst",
]
PAYLOAD_FIELD_CANDIDATES = [
    "usbcom.data.out_payload",
    "usbcom.data.in_payload",
    "usbcom.data",
    "usbcom.control.payload",
    "usbcom.interrupt.payload",
    "usbcom.descriptor.payload",
    "usb.capdata",
    "data.data",
]


def resolve_tshark(explicit: str) -> str | None:
    if explicit and explicit != "tshark":
        if os.path.exists(explicit):
            return explicit
        from_path = shutil.which(explicit)
        return from_path if from_path else None

    found = shutil.which("tshark")
    if found:
        return found

    # Common Windows installation locations.
    for candidate in (
        r"C:\Program Files\Wireshark\tshark.exe",
        r"C:\Program Files (x86)\Wireshark\tshark.exe",
    ):
        if os.path.exists(candidate):
            return candidate
    return None


def get_available_fields(tshark: str) -> set[str]:
    proc = subprocess.run([tshark, "-G", "fields"], capture_output=True, text=True, errors="replace")
    if proc.returncode != 0:
        return set()

    fields: set[str] = set()
    for line in proc.stdout.splitlines():
        # Format: F <tab> Description <tab> abbrev <tab> ...
        if not line.startswith("F\t"):
            continue
        parts = line.split("\t")
        if len(parts) >= 3 and parts[2]:
            fields.add(parts[2].strip())
    return fields


def decode_hex_bytes(raw: str) -> bytes:
    if not raw:
        return b""

    cleaned = raw.replace("\\x", " ").replace("0x", "")
    tokens = [t for t in re.split(r"[:,\s]+", cleaned.strip()) if t]
    out: list[int] = []

    for token in tokens:
        if not re.fullmatch(r"[0-9A-Fa-f]+", token):
            continue
        # Handle contiguous hex stream, e.g. "3132330d".
        if len(token) > 2:
            if len(token) % 2 != 0:
                continue
            pairs = (token[i : i + 2] for i in range(0, len(token), 2))
            out.extend(int(p, 16) for p in pairs)
            continue
        # Handle single nibble defensively.
        if len(token) == 1:
            token = "0" + token
        out.append(int(token, 16))

    return bytes(out)


def to_ascii(data: Iterable[int]) -> str:
    return "".join(chr(b) if 32 <= b <= 126 else "." for b in data)


def to_hex(data: Iterable[int]) -> str:
    return " ".join(f"{b:02X}" for b in data)


def classify_direction(direction: str, src: str, dst: str) -> str:
    d = (direction or "").strip().upper()
    if d in {"0", "0X00", "OUT"}:
        return "Send"
    if d in {"1", "0X01", "IN"}:
        return "Receive"

    src_l = (src or "").strip().lower()
    dst_l = (dst or "").strip().lower()
    if "host" in src_l and "host" not in dst_l:
        return "Send"
    if "host" in dst_l and "host" not in src_l:
        return "Receive"
    return "Unknown"


def merge_direction_events(
    events: list[tuple[str, str, bytes]]
) -> list[tuple[str, str, str, bytes]]:
    """Merge consecutive events with the same direction label."""
    if not events:
        return []

    merged: list[tuple[str, str, str, bytearray]] = []
    for label, frame, payload in events:
        if merged and merged[-1][0] == label:
            prev_label, start_frame, _prev_end, prev_payload = merged[-1]
            prev_payload.extend(payload)
            merged[-1] = (prev_label, start_frame, frame, prev_payload)
        else:
            merged.append((label, frame, frame, bytearray(payload)))

    return [(label, start, end, bytes(payload)) for label, start, end, payload in merged]


def choose_payload_fields(available_fields: set[str]) -> list[str]:
    return [f for f in PAYLOAD_FIELD_CANDIDATES if f in available_fields]


def run(args: argparse.Namespace) -> int:
    tshark = resolve_tshark(args.tshark)
    if not tshark:
        print(
            "Error: tshark not found. Install Wireshark/tshark or pass --tshark <path>.",
            file=sys.stderr,
        )
        return 2

    available_fields = get_available_fields(tshark)
    payload_fields = choose_payload_fields(available_fields) if available_fields else [
        # Fallback: avoid `usbcom.data` because many Wireshark versions do not provide it.
        "usbcom.data.out_payload",
        "usbcom.data.in_payload",
        "usb.capdata",
        "data.data",
    ]

    if not payload_fields:
        print(
            "Error: no supported payload fields found in this tshark version.",
            file=sys.stderr,
        )
        return 2

    tshark_fields = CORE_FIELDS + payload_fields
    cmd = [
        tshark,
        "-r",
        args.capture,
        "-Y",
        args.display_filter,
        "-T",
        "fields",
        "-E",
        "header=n",
        "-E",
        "separator=\t",
        "-E",
        "quote=n",
    ]
    for f in tshark_fields:
        cmd.extend(["-e", f])

    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    if proc.returncode != 0:
        print("tshark failed:", file=sys.stderr)
        print(proc.stderr.strip(), file=sys.stderr)
        return proc.returncode

    lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
    if not lines:
        return 0

    events: list[tuple[str, str, bytes]] = []

    for line in lines:
        cols = line.split("\t")
        cols += [""] * (len(tshark_fields) - len(cols))
        row = dict(zip(tshark_fields, cols))
        frame = row.get("frame.number", "")
        direction = row.get("usb.endpoint_address.direction", "")
        src = row.get("usb.src", "")
        dst = row.get("usb.dst", "")

        decoded_rows: list[tuple[str, bytes]] = []

        out_payload = row.get("usbcom.data.out_payload", "")
        if out_payload:
            decoded = decode_hex_bytes(out_payload)
            if decoded:
                decoded_rows.append(("Send", decoded))

        in_payload = row.get("usbcom.data.in_payload", "")
        if in_payload:
            decoded = decode_hex_bytes(in_payload)
            if decoded:
                decoded_rows.append(("Receive", decoded))

        if not decoded_rows:
            generic_fields = [
                "usbcom.data",
                "usbcom.control.payload",
                "usbcom.interrupt.payload",
                "usbcom.descriptor.payload",
                "usb.capdata",
                "data.data",
            ]
            for field_name in generic_fields:
                payload_raw = row.get(field_name, "")
                if not payload_raw:
                    continue
                payload = decode_hex_bytes(payload_raw)
                if payload:
                    decoded_rows.append((classify_direction(direction, src, dst), payload))
                    break

        if not decoded_rows:
            continue

        for label, payload in decoded_rows:
            events.append((label, frame, payload))

    for label, frame_start, frame_end, payload in merge_direction_events(events):
        if args.show_frame:
            prefix = f"[{frame_start}] " if frame_start == frame_end else f"[{frame_start}-{frame_end}] "
        else:
            prefix = ""
        print(f"{prefix}{label}: {to_ascii(payload)} ({to_hex(payload)})")

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Strip usbcom/usb.capdata payloads from a Wireshark USB capture."
    )
    parser.add_argument("capture", help="Path to .pcap/.pcapng capture file")
    parser.add_argument(
        "--display-filter",
        default=DEFAULT_FILTER,
        help=f'Wireshark display filter (default: "{DEFAULT_FILTER}")',
    )
    parser.add_argument(
        "--tshark",
        default="tshark",
        help="tshark executable path (default: auto-detect in PATH/common locations)",
    )
    parser.add_argument(
        "--show-frame",
        action="store_true",
        help="Prefix each line with frame number",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
