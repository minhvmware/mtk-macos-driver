#!/usr/bin/env python3
"""
Convert an Apple UDIF (.dmg) image (such as macOS Recovery BaseSystem.dmg)
into a raw disk image (.img) suitable for QEMU/KVM without external packages.

Supports sparse output and raw, zero/ignore, zlib, bz2, and lzma blkx chunks.
"""

import bz2
import lzma
import plistlib
import struct
import sys
import zlib
from pathlib import Path

BT_ZERO = 0x00000000
BT_RAW = 0x00000001
BT_IGNORE = 0x00000002
BT_ZLIB = 0x80000005
BT_BZ2 = 0x80000006
BT_LZFSE = 0x80000007
BT_LZMA = 0x80000008
BT_COMMENT = 0x7FFFFFFE
BT_END = 0xFFFFFFFF


def convert_udif_to_raw(dmg_path: Path, raw_path: Path) -> None:
    with dmg_path.open("rb") as f:
        f.seek(0, 2)
        file_size = f.tell()
        if file_size < 512:
            raise ValueError(f"{dmg_path} is too small to be a UDIF DMG")

        f.seek(file_size - 512)
        trailer = f.read(512)
        if trailer[:4] != b"koly":
            raise ValueError(f"{dmg_path} missing 'koly' UDIF trailer")

        data_fork_offset, _data_fork_len = struct.unpack(">QQ", trailer[24:40])
        xml_offset, xml_length = struct.unpack(">QQ", trailer[216:232])
        total_sectors = struct.unpack(">Q", trailer[492:500])[0]

        f.seek(xml_offset)
        xml_bytes = f.read(xml_length)
        plist = plistlib.loads(xml_bytes)

        blkx_list = plist.get("resource-fork", {}).get("blkx", [])
        if not blkx_list:
            raise ValueError("No blkx partitions found in UDIF resource-fork")

        max_end_byte = total_sectors * 512
        with raw_path.open("wb") as out:
            for part in blkx_list:
                mish = part["Data"]
                if mish[:4] != b"mish":
                    raise ValueError("Invalid mish block signature in blkx entry")
                first_sector, part_sectors, mish_data_offset = struct.unpack(">QQQ", mish[8:32])
                num_runs = struct.unpack(">I", mish[200:204])[0]
                part_end = (first_sector + part_sectors) * 512
                if part_end > max_end_byte:
                    max_end_byte = part_end

                pos = 204
                for _ in range(num_runs):
                    (
                        entry_type,
                        _comment,
                        sec_num,
                        sec_cnt,
                        comp_off,
                        comp_len,
                    ) = struct.unpack(">IIQQQQ", mish[pos : pos + 40])
                    pos += 40

                    if entry_type in (BT_COMMENT, BT_END):
                        continue

                    dst_off = (first_sector + sec_num) * 512
                    expected_len = sec_cnt * 512
                    if dst_off + expected_len > max_end_byte:
                        max_end_byte = dst_off + expected_len

                    if entry_type in (BT_ZERO, BT_IGNORE) or expected_len == 0:
                        continue

                    f.seek(data_fork_offset + mish_data_offset + comp_off)
                    chunk = f.read(comp_len)

                    if entry_type == BT_RAW:
                        raw = chunk
                    elif entry_type == BT_ZLIB:
                        raw = zlib.decompress(chunk)
                    elif entry_type == BT_BZ2:
                        raw = bz2.decompress(chunk)
                    elif entry_type == BT_LZMA:
                        raw = lzma.decompress(chunk)
                    elif entry_type == BT_LZFSE:
                        raise RuntimeError("DMG uses LZFSE chunks; fallback converter required")
                    else:
                        raise RuntimeError(f"Unsupported UDIF chunk type: 0x{entry_type:08x}")

                    if len(raw) != expected_len:
                        raise RuntimeError(
                            f"Decompressed size mismatch: got {len(raw)}, expected {expected_len}"
                        )

                    out.seek(dst_off)
                    out.write(raw)

            out.truncate(max_end_byte)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.dmg> <output.img>", file=sys.stderr)
        return 1
    dmg_path = Path(sys.argv[1])
    raw_path = Path(sys.argv[2])
    convert_udif_to_raw(dmg_path, raw_path)
    print(f"[OK] Converted {dmg_path} -> {raw_path} ({raw_path.stat().st_size / (1024**2):.1f} MiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
