#!/usr/bin/env python3
"""Audit actual ESPB instructions emitted into module Code sections."""
from __future__ import annotations

import argparse
import json
import re
import struct
from collections import Counter
from pathlib import Path
from typing import Any

HEADER = struct.Struct("<IIIIH")
SECTION = struct.Struct("<BBHII")
U32 = struct.Struct("<I")
FUNC_HEADER = struct.Struct("<BBHHH")
CODE_SECTION_ID = 6
HOT = 0x40


def _operand_map() -> dict[int, int]:
    result: dict[int, int] = {}

    def put(opcodes, size: int) -> None:
        for opcode in opcodes:
            result[opcode] = size

    put([0x00, 0x01, 0x05, 0x0F, 0xEE], 0)
    put([0x02], 2)
    put([0x03], 3)
    put([0x0A], 2)
    put([0x0B, 0x0D], 3)
    put([0x10, 0x11, 0x12, 0x13], 2)
    put([0x18, 0x1A, 0x1C], 5)
    put([0x19, 0x1B], 9)
    put([0x1D, 0x1E, 0x1F], 3)
    put(range(0x20, 0x25), 3)
    put([0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D], 3)
    put([0x2E], 2)
    put(range(0x30, 0x3E), 3)
    put([0x3E], 2)
    put(range(0x40, 0x4C), 3)
    put(range(0x50, 0x59), 3)
    put(range(0x60, 0x66), 3)
    put([0x66, 0x67], 2)
    put(range(0x68, 0x6E), 3)
    put([0x6E, 0x6F], 2)
    put(range(0x70, 0x75), 4)
    put([0x76, 0x78, 0x79, 0x7A, 0x7B], 4)
    put(range(0x80, 0x8A), 4)
    put([0x8E], 2)
    put([0x8F], 3)
    put([0x90, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
         0x9B, 0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1], 2)
    put(range(0xA4, 0xB6), 2)
    put([0xBC, 0xBD], 2)
    put([0xBE, 0xBF], 4)
    put(range(0xC0, 0xD4), 3)
    put([0xD4, 0xD5, 0xD6], 4)
    put(range(0xD7, 0xDD), 3)
    put([0xDD, 0xF6], 4)
    put([0xDE, 0xDF, 0xEC, 0xED], 2)
    put(range(0xE0, 0xEC), 3)
    put(range(0xF0, 0xF6), 3)
    return result


OPCODE_OPERANDS = _operand_map()
EXTENDED_OPERANDS = {
    0x00: 7, 0x01: 4, 0x02: 3, 0x03: 3, 0x04: 8, 0x06: 3, 0x07: 1,
    0x08: 2, 0x09: 3, 0x0B: 2, 0x16: 5, 0x17: 4, 0x18: 3, 0x19: 3,
}


class AuditError(ValueError):
    pass


def decode_instruction_stream(code: bytes) -> tuple[Counter[int], Counter[int], list[dict[str, Any]]]:
    primary: Counter[int] = Counter()
    extended: Counter[int] = Counter()
    instructions: list[dict[str, Any]] = []
    cursor = 0

    while cursor < len(code):
        offset = cursor
        opcode = code[cursor]
        cursor += 1
        primary[opcode] += 1

        if opcode == 0x04:  # BR_TABLE
            if cursor + 3 > len(code):
                raise AuditError(f"truncated BR_TABLE at byte {offset}")
            target_count = struct.unpack_from("<H", code, cursor + 1)[0]
            operand_size = 5 + target_count * 2
            if cursor + operand_size > len(code):
                raise AuditError(f"truncated BR_TABLE at byte {offset}")
            cursor += operand_size
            instructions.append({"offset": offset, "opcode": opcode, "extended": None})
            continue

        if opcode == 0x09:  # CALL_IMPORT, optional typed variadic tail
            if cursor + 2 > len(code):
                raise AuditError(f"truncated CALL_IMPORT at byte {offset}")
            operand_size = 2
            if cursor + operand_size < len(code) and code[cursor + operand_size] == 0xAA:
                if cursor + operand_size + 2 > len(code):
                    raise AuditError(f"truncated CALL_IMPORT variadic marker at byte {offset}")
                operand_size += 2 + code[cursor + operand_size + 1]
            if cursor + operand_size > len(code):
                raise AuditError(f"truncated CALL_IMPORT operands at byte {offset}")
            cursor += operand_size
            instructions.append({"offset": offset, "opcode": opcode, "extended": None})
            continue

        if opcode == 0xFC:
            if cursor >= len(code):
                raise AuditError(f"missing 0xFC subopcode at byte {offset}")
            subopcode = code[cursor]
            cursor += 1
            if subopcode not in EXTENDED_OPERANDS:
                raise AuditError(f"unknown extended opcode 0xFC.{subopcode:02X} at byte {offset}")
            operand_size = EXTENDED_OPERANDS[subopcode]
            if cursor + operand_size > len(code):
                raise AuditError(f"truncated extended opcode 0xFC.{subopcode:02X} at byte {offset}")
            cursor += operand_size
            extended[subopcode] += 1
            instructions.append({"offset": offset, "opcode": opcode, "extended": subopcode})
            continue

        if opcode not in OPCODE_OPERANDS:
            raise AuditError(f"unknown primary opcode 0x{opcode:02X} at byte {offset}")
        operand_size = OPCODE_OPERANDS[opcode]
        if cursor + operand_size > len(code):
            raise AuditError(
                f"truncated opcode 0x{opcode:02X} at byte {offset}: "
                f"need {operand_size}, have {len(code) - cursor}"
            )
        cursor += operand_size
        instructions.append({"offset": offset, "opcode": opcode, "extended": None})

    return primary, extended, instructions


def _locate_code_section(data: bytes, path: Path) -> tuple[int, int]:
    if len(data) < HEADER.size:
        raise AuditError(f"{path}: truncated ESPB header")
    _, _, _, _, section_count = HEADER.unpack_from(data, 0)
    table_end = HEADER.size + section_count * SECTION.size
    if table_end > len(data):
        raise AuditError(f"{path}: truncated section table")

    found = []
    for index in range(section_count):
        section_id, _, _, offset, size = SECTION.unpack_from(
            data, HEADER.size + index * SECTION.size
        )
        if offset > len(data) or size > len(data) - offset:
            raise AuditError(f"{path}: section {section_id} outside file")
        if section_id == CODE_SECTION_ID:
            found.append((offset, size))
    if len(found) != 1:
        raise AuditError(f"{path}: expected one Code section, found {len(found)}")
    return found[0]


def audit_module(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    section_offset, section_size = _locate_code_section(data, path)
    section_end = section_offset + section_size
    if section_size < U32.size:
        raise AuditError(f"{path}: truncated Code section")
    function_count = U32.unpack_from(data, section_offset)[0]
    cursor = section_offset + U32.size
    primary: Counter[int] = Counter()
    extended: Counter[int] = Counter()
    functions = []
    flag_offsets = []

    for function_index in range(function_count):
        if cursor + U32.size > section_end:
            raise AuditError(f"{path}: missing function {function_index} size")
        body_size = U32.unpack_from(data, cursor)[0]
        cursor += U32.size
        body_start = cursor
        if body_size < FUNC_HEADER.size or body_size > section_end - cursor:
            raise AuditError(f"{path}: invalid function {function_index} body size {body_size}")

        flags, max_reg, frame_size, num_vregs, _ = FUNC_HEADER.unpack_from(data, body_start)
        flag_offsets.append(body_start)
        code_start = body_start + FUNC_HEADER.size
        code_end = body_start + body_size
        function_primary, function_extended, instructions = decode_instruction_stream(
            data[code_start:code_end]
        )
        primary.update(function_primary)
        extended.update(function_extended)
        functions.append({
            "index": function_index,
            "flags": flags,
            "max_reg_used": max_reg,
            "frame_size": frame_size,
            "num_virtual_regs": num_vregs,
            "code_bytes": code_end - code_start,
            "instruction_count": len(instructions),
            "primary": dict(function_primary),
            "extended": dict(function_extended),
        })
        cursor = code_end

    if cursor != section_end:
        raise AuditError(f"{path}: {section_end - cursor} trailing Code section bytes")

    return {
        "path": str(path),
        "name": path.name,
        "function_count": function_count,
        "primary": primary,
        "extended": extended,
        "functions": functions,
        "flag_offsets": flag_offsets,
        "raw": data,
    }


def verify_interpreter_twin(jit: dict[str, Any], interpreter_path: Path) -> None:
    twin = audit_module(interpreter_path)
    expected = bytearray(jit["raw"])
    actual = twin["raw"]
    if len(expected) != len(actual):
        raise AuditError(f"{jit['name']}: paired files have different sizes")
    if len(jit["flag_offsets"]) != len(twin["flag_offsets"]):
        raise AuditError(f"{jit['name']}: paired files have different function counts")

    for index, (jit_offset, interp_offset) in enumerate(
        zip(jit["flag_offsets"], twin["flag_offsets"])
    ):
        if jit_offset != interp_offset:
            raise AuditError(f"{jit['name']}: paired function {index} header offset differs")
        flags = expected[jit_offset]
        if not (flags & HOT):
            raise AuditError(f"{jit['name']}: JIT function {index} is not HOT")
        if actual[interp_offset] & HOT:
            raise AuditError(f"{jit['name']}: interpreter function {index} still has HOT")
        expected[jit_offset] = flags & ~HOT

    if bytes(expected) != actual:
        raise AuditError(f"{jit['name']}: paired files differ beyond HOT flag clearing")


def read_interpreter_dispatch(path: Path) -> set[int]:
    source = path.read_text(encoding="utf-8-sig")
    matches = re.findall(
        r"dispatch_table\[(0x[0-9A-Fa-f]{2})\]\\s*=\\s*&&([A-Za-z0-9_]+)",
        source,
    )
    return {int(op, 16) for op, handler in matches if handler != "op_unhandled"}


def audit_directory(
    paired_dir: Path,
    interpreter_source: Path | None = None,
    expected_groups: int = 10,
) -> dict[str, Any]:
    jit_files = sorted(paired_dir.glob("*.jit.espb"))
    if len(jit_files) != expected_groups:
        raise AuditError(f"expected {expected_groups} JIT modules in {paired_dir}, found {len(jit_files)}")

    rows = []
    total_primary: Counter[int] = Counter()
    total_extended: Counter[int] = Counter()
    for jit_path in jit_files:
        row = audit_module(jit_path)
        twin_path = paired_dir / jit_path.name.replace(".jit.espb", ".interpreter.espb")
        if not twin_path.is_file():
            raise AuditError(f"missing interpreter twin for {jit_path.name}")
        verify_interpreter_twin(row, twin_path)
        total_primary.update(row["primary"])
        total_extended.update(row["extended"])
        rows.append(row)

    interpreter_supported = None
    if interpreter_source:
        interpreter_supported = read_interpreter_dispatch(interpreter_source)
        missing = sorted(set(total_primary) - interpreter_supported)
        if missing:
            raise AuditError(
                "generated primary opcodes missing Interpreter dispatch: "
                + ", ".join(f"0x{x:02X}" for x in missing)
            )

    groups = []
    for row in rows:
        groups.append({
            "name": row["name"],
            "function_count": row["function_count"],
            "functions": row["functions"],
            "primary": {f"0x{k:02X}": v for k, v in sorted(row["primary"].items())},
            "extended": {f"0x{k:02X}": v for k, v in sorted(row["extended"].items())},
        })

    report = {
        "schema": "espb-emitted-opcode-audit/v1",
        "group_count": len(rows),
        "function_count": sum(row["function_count"] for row in rows),
        "primary_instruction_count": sum(total_primary.values()),
        "unique_primary_bytes": [f"0x{x:02X}" for x in sorted(total_primary)],
        "extended_instruction_count": sum(total_extended.values()),
        "unique_extended_subopcodes": [f"0x{x:02X}" for x in sorted(total_extended)],
        "primary_counts": {f"0x{k:02X}": v for k, v in sorted(total_primary.items())},
        "extended_counts": {f"0x{k:02X}": v for k, v in sorted(total_extended.items())},
        "groups": groups,
    }
    if interpreter_supported is not None:
        report["interpreter_supported_count"] = len(interpreter_supported)
        report["interpreter_supported_not_emitted"] = [
            f"0x{x:02X}" for x in sorted(interpreter_supported - set(total_primary))
        ]
    return report


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# Emitted ESPB Opcode Audit",
        "",
        "This report counts decoded instructions from ESPB Code sections, not LLVM IR operators.",
        "",
        f"- Groups: **{report['group_count']}**",
        f"- Functions: **{report['function_count']}**",
        f"- Primary instructions: **{report['primary_instruction_count']}**",
        f"- Distinct primary opcode bytes: **{len(report['unique_primary_bytes'])}**",
        f"- Extended instructions: **{report['extended_instruction_count']}**",
        f"- Distinct extended subopcodes: **{len(report['unique_extended_subopcodes'])}**",
        "",
        "| Group | Functions | Primary instructions | Unique primary | Extended instructions | Unique extended |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for group in report["groups"]:
        primary = group["primary"]
        extended = group["extended"]
        name = group["name"].replace(".jit.espb", "")
        lines.append(
            f"| {name} | {group['function_count']} | {sum(primary.values())} "
            f"| {len(primary)} | {sum(extended.values())} | {len(extended)} |"
        )
    lines.extend([
        "",
        "## Primary opcode histogram",
        "",
        "~~~text",
        " ".join(f"{op}:{count}" for op, count in report["primary_counts"].items()),
        "~~~",
        "",
        "## Extended opcode histogram",
        "",
        "~~~text",
        " ".join(f"FC.{op[2:]}:{count}" for op, count in report["extended_counts"].items())
        or "(none)",
        "~~~",
        "",
    ])
    if "interpreter_supported_not_emitted" in report:
        gaps = report["interpreter_supported_not_emitted"]
        lines.append(
            "Interpreter handlers not emitted by this corpus: "
            + (", ".join(gaps) if gaps else "(none)")
        )
    lines.append(
        "Each JIT/Interpreter pair was checked byte-for-byte after clearing only "
        "the HOT bit in each function header."
    )
    return "\\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--paired-dir", type=Path, default=Path(".work/paired"))
    parser.add_argument("--interpreter-source", type=Path)
    parser.add_argument("--expected-groups", type=int, default=10)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--markdown-out", type=Path)
    args = parser.parse_args()

    try:
        report = audit_directory(args.paired_dir, args.interpreter_source, args.expected_groups)
    except (AuditError, OSError, struct.error) as exc:
        parser.exit(1, f"ESPB_OPCODE_AUDIT_ERROR: {exc}\\n")

    markdown = render_markdown(report)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2) + "\\n", encoding="utf-8")
    if args.markdown_out:
        args.markdown_out.parent.mkdir(parents=True, exist_ok=True)
        args.markdown_out.write_text(markdown + "\\n", encoding="utf-8")
    print(markdown)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
