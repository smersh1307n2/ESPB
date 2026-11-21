#!/usr/bin/env python3
"""Create an interpreter twin by clearing only ESPB_FUNC_FLAG_HOT in Code bodies."""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

HEADER = struct.Struct("<IIIIH")
SECTION = struct.Struct("<BBHII")
U32 = struct.Struct("<I")
HOT = 0x40
CODE_SECTION_ID = 6


def make_interpreter_variant(source: Path, destination: Path) -> tuple[int, int]:
    data = bytearray(source.read_bytes())
    if len(data) < HEADER.size:
        raise ValueError(f"{source}: truncated ESPB header")

    _magic, _version, _flags, _features, section_count = HEADER.unpack_from(data, 0)
    table_end = HEADER.size + section_count * SECTION.size
    if table_end > len(data):
        raise ValueError(f"{source}: truncated section table")

    code_sections = []
    for index in range(section_count):
        entry_at = HEADER.size + index * SECTION.size
        section_id, _reserved8, _reserved16, offset, size = SECTION.unpack_from(data, entry_at)
        if offset > len(data) or size > len(data) - offset:
            raise ValueError(f"{source}: section {section_id} is out of bounds")
        if section_id == CODE_SECTION_ID:
            code_sections.append((offset, size))

    if len(code_sections) != 1:
        raise ValueError(f"{source}: expected exactly one Code section, got {len(code_sections)}")

    code_offset, code_size = code_sections[0]
    code_end = code_offset + code_size
    if code_size < U32.size:
        raise ValueError(f"{source}: truncated Code section")

    body_count = U32.unpack_from(data, code_offset)[0]
    cursor = code_offset + U32.size
    if body_count == 0:
        raise ValueError(f"{source}: Code section contains no function bodies")

    hot_count = 0
    for body_index in range(body_count):
        if cursor + U32.size > code_end:
            raise ValueError(f"{source}: missing body size for function {body_index}")
        body_size = U32.unpack_from(data, cursor)[0]
        cursor += U32.size
        if body_size < 8 or body_size > code_end - cursor:
            raise ValueError(f"{source}: invalid body size for function {body_index}")
        flags_offset = cursor
        flags = data[flags_offset]
        if not (flags & HOT):
            raise ValueError(
                f"{source}: function body {body_index} is not HOT; "
                "refusing to produce a mismatched interpreter twin"
            )
        hot_count += 1
        data[flags_offset] = flags & ~HOT
        cursor += body_size

    if cursor != code_end:
        raise ValueError(f"{source}: unexpected trailing bytes in Code section")

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    return body_count, hot_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    body_count, hot_count = make_interpreter_variant(args.source, args.destination)
    print(
        f"INTERPRETER_TWIN={args.destination.name} "
        f"FUNCTIONS={body_count} HOT_CLEARED={hot_count}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
