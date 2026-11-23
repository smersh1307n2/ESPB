#!/usr/bin/env python3
"""Translate LLVM bitcode groups into paired .jit.espb / .interpreter.espb using projects/llvm_espb."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "bytecode_groups"))
sys.path.insert(0, str(ROOT / "tests" / "host_tools"))

from make_interpreter_variant import CODE_SECTION_ID, HEADER, SECTION, U32, make_interpreter_variant  # noqa: E402
from audit_espb_opcodes import audit_directory, decode_instruction_stream  # noqa: E402

FUNC_HEADER = struct.Struct("<BBHHH")


def check_module_register_bounds(module_path: Path) -> None:
    """Verify every register operand in every function body is <= max_reg_used and < num_vregs."""
    data = module_path.read_bytes()
    _magic, _ver, _flags, _feat, section_count = HEADER.unpack_from(data, 0)
    code_offset = code_size = None
    for i in range(section_count):
        sid, _r8, _r16, off, sz = SECTION.unpack_from(data, HEADER.size + i * SECTION.size)
        if sid == CODE_SECTION_ID:
            code_offset, code_size = off, sz
            break
    if code_offset is None:
        raise ValueError(f"{module_path.name}: missing Code section")

    body_count = U32.unpack_from(data, code_offset)[0]
    cursor = code_offset + U32.size
    for func_idx in range(body_count):
        body_size = U32.unpack_from(data, cursor)[0]
        cursor += U32.size
        body = data[cursor : cursor + body_size]
        cursor += body_size
        _flags, max_reg_used, _frame_size, num_vregs, _res = FUNC_HEADER.unpack_from(body, 0)
        if num_vregs == 0 or max_reg_used >= num_vregs:
            raise ValueError(
                f"{module_path.name} func {func_idx}: invalid header max_reg={max_reg_used} num_vregs={num_vregs}"
            )
        code = body[FUNC_HEADER.size :]
        _pri, _ext, instrs = decode_instruction_stream(code)
        for ins in instrs:
            off = ins["offset"]
            op = ins["opcode"]
            ext = ins["extended"]
            regs: list[int] = []
            if op in (0x03, 0x04, 0x08, 0x0C, 0x0D, 0x8F) or (0x18 <= op <= 0x1F):
                regs = [code[off + 1]]
            elif (
                op in (0x0B, 0x2E, 0x2F, 0x3E, 0x3F, 0x66, 0x67, 0x6E, 0x6F, 0x8E, 0xDE, 0xDF, 0xEC, 0xED)
                or (0x10 <= op <= 0x17)
                or (0x40 <= op <= 0x5F)
                or (0x70 <= op <= 0x7B)
                or (0x80 <= op <= 0x8D)
                or (0x90 <= op <= 0xBD)
            ):
                regs = [code[off + 1], code[off + 2]]
            elif (
                (0x20 <= op <= 0x2D)
                or (0x30 <= op <= 0x3D)
                or (0x60 <= op <= 0x65)
                or (0x68 <= op <= 0x6D)
                or (0xC0 <= op <= 0xD3)
                or (0xD7 <= op <= 0xDC)
                or (0xE0 <= op <= 0xEB)
                or (0xF0 <= op <= 0xF5)
            ):
                regs = [code[off + 1], code[off + 2], code[off + 3]]
            elif op in (0xBE, 0xBF, 0xD4, 0xD5, 0xD6, 0xDD, 0xF6):
                regs = [code[off + 1], code[off + 2], code[off + 3], code[off + 4]]
            elif op == 0xFC and ext in (0x00, 0x02, 0x03, 0x04, 0x05):
                if ext == 0x00:
                    regs = [code[off + 6], code[off + 7], code[off + 8]]
                elif ext == 0x02:
                    regs = [code[off + 2], code[off + 3], code[off + 4]]
                elif ext == 0x03:
                    regs = [code[off + 2], code[off + 3]]
                else:
                    regs = [code[off + 2]]

            for r in regs:
                if r > max_reg_used or r >= num_vregs:
                    raise ValueError(
                        f"{module_path.name} func {func_idx} @+{off} op=0x{op:02X}: "
                        f"register R{r} out of bounds (max_reg_used={max_reg_used}, num_vregs={num_vregs})"
                    )


def git_blob_sha(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def build_seed_manifest(output_dir: Path, branch: str = "ESPB_WORK") -> dict:
    groups = []
    for jit_path in sorted(output_dir.glob("*.jit.espb")):
        stem = jit_path.name[: -len(".jit.espb")]
        interp_path = output_dir / f"{stem}.interpreter.espb"
        if not interp_path.is_file():
            raise FileNotFoundError(f"Missing interpreter twin for {stem}: {interp_path}")
        qemu_group = stem.split("_", 1)[1] if "_" in stem else stem
        jit_bytes = jit_path.read_bytes()
        interp_bytes = interp_path.read_bytes()
        groups.append(
            {
                "name": stem,
                "qemu_group": qemu_group,
                "jit": jit_path.name,
                "jit_git_blob_sha": git_blob_sha(jit_bytes),
                "jit_size_bytes": len(jit_bytes),
                "interpreter": interp_path.name,
                "interpreter_git_blob_sha": git_blob_sha(interp_bytes),
                "interpreter_size_bytes": len(interp_bytes),
            }
        )
    return {
        "schema": "espb-qemu-paired-tests/v1",
        "branch": branch,
        "group_count": len(groups),
        "groups": groups,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bitcode-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument(
        "--translator-dll",
        type=Path,
        default=ROOT / "projects" / "llvm_espb" / "bin" / "Release" / "net9.0" / "LLVM_ESPB.dll",
    )
    parser.add_argument("--custom-fast", type=Path, default=None)
    parser.add_argument("--idf-fast", type=Path, default=None)
    parser.add_argument("--hints-dir", type=Path, default=None)
    parser.add_argument("--no-patch-regs", action="store_true")
    parser.add_argument("--groups", nargs="+", default=None)
    parser.add_argument("--audit-json", type=Path, default=None)
    parser.add_argument("--update-qemu-seed", type=Path, default=None)
    args = parser.parse_args()

    if not args.translator_dll.is_file():
        raise SystemExit(f"LLVM_ESPB.dll not found at {args.translator_dll}")

    bc_files = {p.stem: p for p in sorted(args.bitcode_dir.glob("*.bc"))}
    if not bc_files:
        raise SystemExit(f"No .bc files found in {args.bitcode_dir}")

    if args.groups:
        unknown = sorted(set(args.groups) - set(bc_files))
        if unknown:
            raise SystemExit(f"Unknown bitcode group(s): {', '.join(unknown)}")
        selected = [bc_files[g] for g in dict.fromkeys(args.groups)]
    else:
        selected = list(bc_files.values())

    args.output_dir.mkdir(parents=True, exist_ok=True)
    base_cmd = ["dotnet", str(args.translator_dll)]
    if not args.no_patch_regs:
        base_cmd.append("--patch-regs")
    if args.custom_fast:
        base_cmd.extend(["--custom-fast", str(args.custom_fast)])
    if args.idf_fast:
        base_cmd.extend(["--idf-fast", str(args.idf_fast)])

    for bc_path in selected:
        jit_out = args.output_dir / f"{bc_path.stem}.jit.espb"
        interp_out = args.output_dir / f"{bc_path.stem}.interpreter.espb"
        cmd = list(base_cmd)
        if args.hints_dir:
            hint_file = args.hints_dir / f"{bc_path.stem}.hints"
            if hint_file.is_file():
                cmd.extend(["-hi", str(hint_file)])
        cmd.extend([str(bc_path), str(jit_out)])
        print(f"Translating {bc_path.name} -> {jit_out.name}", flush=True)
        subprocess.run(cmd, check=True)
        if not jit_out.is_file() or jit_out.stat().st_size == 0:
            raise RuntimeError(f"Translator did not produce output: {jit_out}")
        make_interpreter_variant(jit_out, interp_out)
        check_module_register_bounds(jit_out)
        check_module_register_bounds(interp_out)
        print(f"Verified {bc_path.stem}: jit={jit_out.stat().st_size}B interp={interp_out.stat().st_size}B", flush=True)

    manifest = build_seed_manifest(args.output_dir)
    (args.output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    interp_src = ROOT / "components" / "espb" / "src" / "espb_interpreter_runtime_oc.c"
    audit = audit_directory(
        args.output_dir,
        interpreter_source=interp_src if interp_src.is_file() else None,
        expected_groups=len(selected),
    )
    if args.audit_json:
        args.audit_json.parent.mkdir(parents=True, exist_ok=True)
        args.audit_json.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    print(
        f"OPCODE_AUDIT_PASSED groups={audit['group_count']} total_instr={audit['primary_instruction_count']}",
        flush=True,
    )

    if args.update_qemu_seed:
        args.update_qemu_seed.mkdir(parents=True, exist_ok=True)
        for p in args.output_dir.glob("*.espb"):
            shutil.copy2(p, args.update_qemu_seed / p.name)
        seed_manifest = build_seed_manifest(args.update_qemu_seed)
        (args.update_qemu_seed / "manifest.json").write_text(json.dumps(seed_manifest, indent=2) + "\n", encoding="utf-8")
        print(f"Updated {args.update_qemu_seed} ({seed_manifest['group_count']} groups)", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
