#!/usr/bin/env python3
"""Generate paired interpreter-only and HOT/JIT ESPB modules for QEMU tests."""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

HEADER_SIZE = 18
SECTION_ENTRY_SIZE = 12
CODE_SECTION_ID = 6
HOT_FLAG = 0x40


def set_hot_flags(source: bytes, hot: bool, label: str) -> tuple[bytes, int]:
    data = bytearray(source)
    if len(data) < HEADER_SIZE:
        raise ValueError(f"{label}: truncated ESPB header")

    section_count = struct.unpack_from("<H", data, 16)[0]
    table_end = HEADER_SIZE + section_count * SECTION_ENTRY_SIZE
    if section_count == 0 or table_end > len(data):
        raise ValueError(f"{label}: invalid section table")

    code_sections: list[tuple[int, int]] = []
    for index in range(section_count):
        entry = HEADER_SIZE + index * SECTION_ENTRY_SIZE
        section_id = data[entry]
        section_offset = struct.unpack_from("<I", data, entry + 4)[0]
        section_size = struct.unpack_from("<I", data, entry + 8)[0]
        if section_offset > len(data) or section_size > len(data) - section_offset:
            raise ValueError(f"{label}: section {index} is out of bounds")
        if section_id == CODE_SECTION_ID:
            code_sections.append((section_offset, section_size))

    if len(code_sections) != 1:
        raise ValueError(f"{label}: expected exactly one Code section, found {len(code_sections)}")

    code_offset, code_size = code_sections[0]
    code_end = code_offset + code_size
    if code_size < 4:
        raise ValueError(f"{label}: truncated Code section")

    body_count = struct.unpack_from("<I", data, code_offset)[0]
    cursor = code_offset + 4
    changed = 0
    for body_index in range(body_count):
        if cursor + 4 > code_end:
            raise ValueError(f"{label}: missing size for function body {body_index}")
        body_size = struct.unpack_from("<I", data, cursor)[0]
        body_start = cursor + 4
        body_end = body_start + body_size
        if body_size < 8 or body_end > code_end:
            raise ValueError(f"{label}: invalid function body {body_index} size {body_size}")
        flags_offset = body_start
        flags = data[flags_offset]
        new_flags = (flags | HOT_FLAG) if hot else (flags & ~HOT_FLAG)
        if new_flags != flags:
            data[flags_offset] = new_flags
            changed += 1
        cursor = body_end

    if cursor != code_end:
        raise ValueError(f"{label}: {code_end - cursor} unparsed trailing bytes in Code section")
    return bytes(data), body_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-files", nargs="+", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    inputs = sorted({p.resolve() for p in args.input_files if p.is_file()})
    missing = [str(p) for p in args.input_files if not p.is_file()]
    if missing:
        parser.error("input module(s) not found: " + ", ".join(missing))
    if not inputs:
        parser.error("no ESPB input modules were supplied")

    names = [p.name for p in inputs]
    if len(names) != len(set(names)):
        parser.error("input module basenames must be unique")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for pattern in ("interp__*.espb", "jit__*.espb"):
        for stale in args.output_dir.glob(pattern):
            stale.unlink()

    total_bodies = 0
    for source_path in inputs:
        source = source_path.read_bytes()
        for prefix, hot in (("interp__", False), ("jit__", True)):
            generated, body_count = set_hot_flags(source, hot, str(source_path))
            destination = args.output_dir / f"{prefix}{source_path.name}"
            destination.write_bytes(generated)
            total_bodies += body_count if hot else 0
            print(f"{destination.name}: mode={'HOT/JIT' if hot else 'interpreter'}, functions={body_count}")

    print(f"Prepared {len(inputs)} source modules as {2 * len(inputs)} variants; JIT functions={total_bodies}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
