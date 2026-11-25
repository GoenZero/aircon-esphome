"""Collect the build output of one configuration and check it against the device.

Usage: python tools/collect.py <config-name> <device-name> <out-dir>

<device-name> is `esphome: name:` (both configurations build the same device, so the
build directory is named after the device, not the file).

The build fails unless:
- the partition table is exactly the one already on the NanoC6 (written by the first
  factory flash; OTA never changes it): every entry, offset, size and flag;
- the OTA image fits one app slot;
- esptool reads the OTA image as a valid ESP32-C6 app (checksum and hash valid).
Writes a short report to $GITHUB_STEP_SUMMARY when it is set, and the key facts as
workflow annotations (readable through the public check-runs API without signing in).
"""

from __future__ import annotations

import hashlib
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

# label: (type, subtype, offset, size, flags), read from the factory image on the device
ON_DEVICE = {
    "otadata": (1, 0x00, 0x9000, 0x2000, 0),
    "phy_init": (1, 0x01, 0xB000, 0x1000, 0),
    "app0": (0, 0x10, 0x10000, 0x1C0000, 0),
    "app1": (0, 0x11, 0x1D0000, 0x1C0000, 0),
    "nvs": (1, 0x02, 0x390000, 0x70000, 0),
}
SLOT_SIZE = 0x1C0000


def find(root: Path, names: list[str]) -> Path | None:
    hits = [p for name in names for p in root.rglob(name)]
    return max(hits, key=lambda p: p.stat().st_mtime) if hits else None


def parse_partitions(data: bytes) -> dict[str, tuple[int, int, int, int, int]]:
    table = {}
    for i in range(0, len(data), 32):
        entry = data[i : i + 32]
        if entry[:2] != b"\xaa\x50":
            break
        ptype, subtype = entry[2], entry[3]
        offset, size = struct.unpack("<II", entry[4:12])
        label = entry[12:28].rstrip(b"\0").decode()
        flags = struct.unpack("<I", entry[28:32])[0]
        table[label] = (ptype, subtype, offset, size, flags)
    return table


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def image_info(path: Path) -> str | None:
    for cmd in (["esptool", "--chip", "esp32c6", "image-info", str(path)],
                ["esptool.py", "--chip", "esp32c6", "image_info", str(path)]):
        try:
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        if res.returncode == 0:
            return res.stdout
    return None


def main() -> int:
    name, device, out = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    root = Path(".esphome") / "build" / device
    ota = find(root, ["firmware.ota.bin"])
    factory = find(root, ["firmware.factory.bin"])
    parts = find(root, ["partitions.bin", "partition-table.bin"])
    if ota is None or parts is None:
        print(f"build output not found under {root}", file=sys.stderr)
        for p in sorted(root.rglob("*.bin")):
            print("  ", p, file=sys.stderr)
        return 1

    report = [f"## {name}", ""]
    problems = []

    table = parse_partitions(parts.read_bytes())
    report.append("| partition | type | subtype | offset | size | flags | matches device |")
    report.append("|---|---|---|---|---|---|---|")
    for label, (ptype, subtype, offset, size, flags) in table.items():
        match = ON_DEVICE.get(label) == (ptype, subtype, offset, size, flags)
        report.append(f"| {label} | {ptype} | 0x{subtype:02x} | 0x{offset:06x} | 0x{size:06x} | 0x{flags:x} | {'yes' if match else 'NO'} |")
    table_ok = table == ON_DEVICE
    if not table_ok:
        problems.append("partition table differs from the device")

    size = ota.stat().st_size
    used = size / SLOT_SIZE * 100
    report.append("")
    report.append(f"- OTA image: {size:,} bytes, {used:.1f}% of a {SLOT_SIZE:,}-byte slot, {SLOT_SIZE - size:,} bytes free")
    if size >= SLOT_SIZE:
        problems.append("OTA image does not fit the slot")
    elif used > 95:
        report.append("- Warning: over 95% of the slot.")

    info = image_info(ota)
    info_ok = info is not None and "ESP32-C6" in info and info.count("(valid)") >= 2
    if not info_ok:
        problems.append("esptool did not confirm a valid ESP32-C6 image")
    else:
        report += ["", "<details><summary>esptool image-info</summary>", "", "```", info.strip(), "```", "</details>"]

    out.mkdir(parents=True, exist_ok=True)
    copies = {ota: f"{name}.ota.bin", parts: f"{name}.partitions.bin"}
    if factory is not None:
        copies[factory] = f"{name}.factory.bin"
    report.append("")
    for src, dst in copies.items():
        shutil.copy2(src, out / dst)
        report.append(f"- `{dst}` sha256 `{sha256(out / dst)}`")

    for p in problems:
        report.append(f"- **{p}**")

    layout = ", ".join(f"{k}@0x{v[2]:x}/0x{v[3]:x}/f{v[4]:x}" for k, v in table.items())
    level = "error" if problems else "notice"
    print(f"::{level} title={name} image::{size} bytes, {used:.1f}% of slot, {SLOT_SIZE - size} free; "
          f"partition table matches device: {table_ok}; esptool valid: {info_ok}"
          + (f"; problems: {', '.join(problems)}" if problems else ""))
    print(f"::notice title={name} partitions::{layout}")
    print(f"::notice title={name} sha256::ota {sha256(out / f'{name}.ota.bin')}")

    text = "\n".join(report) + "\n"
    print(text)
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write(text)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
