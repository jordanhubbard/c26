#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a blank c26 disk image")
    parser.add_argument("path", type=Path)
    parser.add_argument("--size", type=int, default=8 * 1024 * 1024)
    args = parser.parse_args()
    args.path.parent.mkdir(parents=True, exist_ok=True)
    if not args.path.exists():
        with args.path.open("wb") as disk:
            disk.truncate(args.size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
