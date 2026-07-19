#!/usr/bin/env python3
"""Install files into a C26FS v2 disk image from the host.

Implements the same on-disk layout as src/fs.c (keep them in sync):
sector 0 superblock, sectors 1-4 directory (64 x 32-byte entries),
sectors 5-8 allocation bitmap, data from sector 9. Formats blank images;
updates images the guest has already formatted, which doubles as a
compatibility check between this implementation and the C one.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SECTOR = 512
MAGIC = 0x46363243  # 'C26F'
VERSION = 2
DIR_START = 1
DIR_SECTORS = 4
MAP_START = 5
MAP_SECTORS = 4
DATA_START = 9
FILE_COUNT = 64
NAME_MAX = 15
FILE_MAX = 128 * 1024
MAP_BYTES = MAP_SECTORS * SECTOR

SUPER_FMT = "<13I460x"
ENTRY_FMT = "<16s4I"


def checksum(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


class Filesystem:
    def __init__(self, image: bytearray):
        self.image = image
        self.total_sectors = min(len(image) // SECTOR, MAP_BYTES * 8)
        fields = struct.unpack_from(SUPER_FMT, image, 0)
        self.generation = fields[9]
        directory = self._read(DIR_START, DIR_SECTORS)
        bitmap = self._read(MAP_START, MAP_SECTORS)
        valid = (
            fields[0] == MAGIC
            and fields[1] == VERSION
            and fields[2] == SECTOR
            and fields[3] <= self.total_sectors
            and fields[10] == checksum(directory)
            and fields[11] == checksum(bitmap)
        )
        if valid:
            self.total_sectors = fields[3]
            self.directory = bytearray(directory)
            self.bitmap = bytearray(bitmap)
        else:
            self.directory = bytearray(DIR_SECTORS * SECTOR)
            self.bitmap = bytearray(MAP_SECTORS * SECTOR)
            for sector in range(DATA_START):
                self._mark(sector, True)
            self.generation = 0

    def _read(self, start: int, count: int) -> bytes:
        return bytes(self.image[start * SECTOR:(start + count) * SECTOR])

    def _write(self, start: int, data: bytes) -> None:
        self.image[start * SECTOR:start * SECTOR + len(data)] = data

    def _mark(self, sector: int, used: bool) -> None:
        if used:
            self.bitmap[sector // 8] |= 1 << (sector % 8)
        else:
            self.bitmap[sector // 8] &= ~(1 << (sector % 8)) & 0xFF

    def _used(self, sector: int) -> bool:
        return bool(self.bitmap[sector // 8] >> (sector % 8) & 1)

    def _entries(self):
        for index in range(FILE_COUNT):
            offset = index * 32
            name_raw, size, start, sectors, check = struct.unpack_from(
                ENTRY_FMT, self.directory, offset)
            name = name_raw.split(b"\0", 1)[0].decode("ascii", "replace")
            yield index, name, size, start, sectors, check

    def _alloc(self, count: int) -> int:
        run_start = 0
        run_length = 0
        for sector in range(DATA_START, self.total_sectors):
            if self._used(sector):
                run_length = 0
                continue
            if run_length == 0:
                run_start = sector
            run_length += 1
            if run_length == count:
                for used in range(run_start, run_start + count):
                    self._mark(used, True)
                return run_start
        raise SystemExit("fsinstall: disk full")

    def install(self, name: str, data: bytes) -> None:
        if not (0 < len(name) <= NAME_MAX) or not all(
                c.isupper() or c.isdigit() or c in "_-." for c in name):
            raise SystemExit(f"fsinstall: invalid name {name!r}")
        if not 0 < len(data) <= FILE_MAX:
            raise SystemExit(f"fsinstall: bad size for {name}")
        sectors = (len(data) + SECTOR - 1) // SECTOR
        slot = None
        for index, existing, _size, start, count, _check in self._entries():
            if existing == name:
                for sector in range(start, start + count):
                    self._mark(sector, False)
                slot = index
                break
            if slot is None and existing == "":
                slot = index
        if slot is None:
            raise SystemExit("fsinstall: directory full")
        start = self._alloc(sectors)
        padded = data + b"\0" * (sectors * SECTOR - len(data))
        self._write(start, padded)
        struct.pack_into(ENTRY_FMT, self.directory, slot * 32,
                         name.encode("ascii"), len(data), start, sectors,
                         checksum(data))

    def flush(self) -> None:
        self.generation += 1
        self._write(DIR_START, bytes(self.directory))
        self._write(MAP_START, bytes(self.bitmap))
        super_block = bytearray(SECTOR)
        struct.pack_into(
            SUPER_FMT, super_block, 0,
            MAGIC, VERSION, SECTOR, self.total_sectors,
            DIR_START, DIR_SECTORS, MAP_START, MAP_SECTORS, DATA_START,
            self.generation, checksum(bytes(self.directory)),
            checksum(bytes(self.bitmap)), 0)
        struct.pack_into("<I", super_block, 12 * 4, checksum(bytes(super_block)))
        self._write(0, bytes(super_block))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("installs", nargs="+", metavar="NAME=FILE",
                        help="e.g. PAINT=build/paint.cart")
    args = parser.parse_args()
    image = bytearray(args.image.read_bytes())
    if len(image) < DATA_START * SECTOR:
        sys.stderr.write("fsinstall: image too small\n")
        return 1
    fs = Filesystem(image)
    for spec in args.installs:
        name, _, source = spec.partition("=")
        fs.install(name.upper(), Path(source).read_bytes())
    fs.flush()
    args.image.write_bytes(bytes(image))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
