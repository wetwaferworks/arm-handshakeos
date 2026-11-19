#!/usr/bin/env python3
"""Build a gzipped newc initramfs with litho-init as /init and /dev/console."""
import gzip
import os
import sys
import time


def add(entries, name, data=b"", mode=0o100755, maj=0, mino=0):
    enc = name.encode() + b"\0"
    ino = len(entries) + 1
    typ = mode & 0o170000
    nlink = 2 if typ == 0o040000 else 1
    if typ in (0o040000, 0o020000, 0o060000):
        filesize = 0
        payload = b""
    else:
        payload = data or b""
        filesize = len(payload)
    hdr = (
        "%s%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x"
        % (
            "070701",
            ino,
            mode,
            0,
            0,
            nlink,
            int(time.time()),
            filesize,
            0,
            0,
            maj,
            mino,
            len(enc),
            0,
        )
    ).encode("ascii")
    pad_hdr = (4 - (len(hdr) + len(enc)) % 4) % 4
    pad_data = (4 - filesize % 4) % 4
    entries.append(hdr + enc + b"\0" * pad_hdr + payload + b"\0" * pad_data)


def main():
    if len(sys.argv) != 4:
        sys.stderr.write("usage: pack-initramfs.py INIT ROM OUT.cpio.gz\n")
        return 2
    init, rom, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    entries = []
    add(entries, ".", mode=0o040755)
    add(entries, "dev", mode=0o040755)
    add(entries, "proc", mode=0o040755)
    add(entries, "sys", mode=0o040755)
    add(entries, "roms", mode=0o040755)
    add(entries, "dev/console", mode=0o020622, maj=5, mino=1)
    add(entries, "dev/null", mode=0o020666, maj=1, mino=3)
    with open(init, "rb") as f:
        add(entries, "init", f.read(), mode=0o100755)
    with open(rom, "rb") as f:
        add(entries, "roms/litho-demo.gb", f.read(), mode=0o100644)
    add(entries, "TRAILER!!!", data=b"", mode=0)
    blob = b"".join(entries)
    parent = os.path.dirname(out_path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    with gzip.open(out_path, "wb", compresslevel=9) as z:
        z.write(blob)
    print(
        "initramfs %s (%d bytes packed, %d gz)"
        % (out_path, len(blob), os.path.getsize(out_path))
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
