#!/usr/bin/env python3
"""Extract the game tree from the UMD image described by ISO9660.

The bring-up harness resolves `disc0:/...` against `extract/`, so a partial extraction shows up
as a file the guest keeps retrying to open - not as an error. This walks the whole primary volume
descriptor instead of a hand-picked list, and follows multi-extent records so large files are not
truncated to their first extent.

Usage: python scripts/extract_iso.py [--iso PATH] [--out DIR]
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

SECTOR = 2048
# Records with this flag continue in the next record with the same name.
MULTI_EXTENT = 0x80
DIRECTORY = 0x02


def read_sector(image, sector: int, count: int = 1) -> bytes:
    image.seek(sector * SECTOR)
    return image.read(SECTOR * count)


def parse_records(data: bytes):
    offset = 0
    while offset < len(data):
        length = data[offset]
        if length == 0:
            # Records never straddle a sector boundary; the rest of this sector is padding.
            offset = (offset // SECTOR + 1) * SECTOR
            continue
        extent = struct.unpack_from("<I", data, offset + 2)[0]
        size = struct.unpack_from("<I", data, offset + 10)[0]
        flags = data[offset + 25]
        name_length = data[offset + 32]
        name = data[offset + 33 : offset + 33 + name_length]
        if name not in (b"\x00", b"\x01"):
            yield extent, size, flags, name.decode("ascii", "replace")
        offset += length


def primary_root(image):
    descriptor = read_sector(image, 16)
    if descriptor[1:6] != b"CD001":
        raise SystemExit("not an ISO9660 image: missing CD001 signature at sector 16")
    return struct.unpack_from("<I", descriptor, 156 + 2)[0], struct.unpack_from("<I", descriptor, 156 + 10)[0]


def read_directory(image, extent: int, size: int):
    image.seek(extent * SECTOR)
    return image.read(size)


def extract(image, out_root: str) -> tuple[int, int]:
    files = 0
    written = 0
    pending = [(primary_root(image)[0], primary_root(image)[1], "")]
    while pending:
        extent, size, path = pending.pop()
        data = read_directory(image, extent, size)
        # Group consecutive records by name so a multi-extent file is assembled in order.
        runs: dict[str, list[tuple[int, int, int]]] = {}
        for record_extent, record_size, flags, name in parse_records(data):
            if name in (".", ".."):
                continue
            clean = name.split(";")[0]
            runs.setdefault(clean, []).append((record_extent, record_size, flags))

        for clean, records in runs.items():
            child = os.path.join(path, clean)
            if records[0][2] & DIRECTORY:
                pending.append((records[0][0], records[0][1], child))
                continue
            target = os.path.join(out_root, child)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as output:
                for record_extent, record_size, _ in records:
                    image.seek(record_extent * SECTOR)
                    remaining = record_size
                    while remaining > 0:
                        chunk = image.read(min(remaining, 1024 * 1024))
                        if not chunk:
                            break
                        output.write(chunk)
                        remaining -= len(chunk)
            files += 1
            written += sum(record[1] for record in records)
    return files, written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", default="Metal Gear Ac!d (Europe) (En,Fr,De,Es,It).iso")
    parser.add_argument("--out", default="extract")
    arguments = parser.parse_args()

    with open(arguments.iso, "rb") as image:
        files, written = extract(image, arguments.out)
    print(f"extracted {files} files, {written / (1024 * 1024):.1f} MiB into {arguments.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
