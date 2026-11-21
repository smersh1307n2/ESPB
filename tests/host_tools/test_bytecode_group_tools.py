#!/usr/bin/env python3
from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TOOLS_DIR = REPO_ROOT / "tools" / "bytecode_groups"
sys.path.insert(0, str(TOOLS_DIR))

from corpus_manifest import ManifestError, create_manifest, verify_manifest  # noqa: E402
from clang_group_wrapper import ATOMIC_MARCH, FLOAT_OPTIONS, adjust_clang_args  # noqa: E402
from translator_keys import collect_translator_keys  # noqa: E402
sys.path.insert(0, str(REPO_ROOT / ".github" / "scripts"))
from reuse_paired_groups import ReuseError, reuse_unchanged_pairs  # noqa: E402
from audit_espb_opcodes import (  # noqa: E402
    AuditError,
    audit_directory,
    decode_instruction_stream,
    render_markdown,
    verify_interpreter_twin,
)
from make_interpreter_variant import (  # noqa: E402
    CODE_SECTION_ID,
    HEADER,
    HOT,
    SECTION,
    make_interpreter_variant,
)


def make_test_module(function_flags: tuple[int, ...] = (HOT, HOT)) -> bytes:
    code = bytearray(struct.pack("<I", len(function_flags)))
    for flags in function_flags:
        code += struct.pack("<I", 8)
        code += bytes([flags]) + b"\x11\x22\x33\x44\x55\x66\x77"
    section_offset = HEADER.size + SECTION.size
    header = HEADER.pack(0x42505345, 1, 0, 0, 1)
    section = SECTION.pack(CODE_SECTION_ID, 0, 0, section_offset, len(code))
    return header + section + bytes(code)


class InterpreterVariantTests(unittest.TestCase):
    def test_clears_only_hot_for_every_function(self) -> None:
        source_data = make_test_module((HOT, HOT | 0x01, HOT | 0x20))
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "jit.espb"
            destination = Path(temporary) / "interpreter.espb"
            source.write_bytes(source_data)

            function_count, hot_count = make_interpreter_variant(source, destination)

            expected = bytearray(source_data)
            code_offset = HEADER.size + SECTION.size
            cursor = code_offset + 4
            for _ in range(function_count):
                body_size = struct.unpack_from("<I", expected, cursor)[0]
                cursor += 4
                expected[cursor] &= ~HOT
                cursor += body_size

            self.assertEqual((function_count, hot_count), (3, 3))
            self.assertEqual(destination.read_bytes(), bytes(expected))

    def test_rejects_function_without_hot_flag(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "jit.espb"
            destination = Path(temporary) / "interpreter.espb"
            source.write_bytes(make_test_module((HOT, 0x20)))
            with self.assertRaisesRegex(ValueError, "is not HOT"):
                make_interpreter_variant(source, destination)

    def test_rejects_trailing_code_section_bytes(self) -> None:
        data = bytearray(make_test_module())
        section_size = struct.unpack_from("<I", data, HEADER.size + 8)[0]
        data += b"\x00"
        struct.pack_into("<I", data, HEADER.size + 8, section_size + 1)
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "jit.espb"
            destination = Path(temporary) / "interpreter.espb"
            source.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "trailing bytes"):
                make_interpreter_variant(source, destination)

    def test_rejects_out_of_bounds_code_section(self) -> None:
        data = bytearray(make_test_module())
        SECTION.pack_into(data, HEADER.size, CODE_SECTION_ID, 0, 0, len(data) + 4, 8)
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "jit.espb"
            destination = Path(temporary) / "interpreter.espb"
            source.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "out of bounds"):
                make_interpreter_variant(source, destination)


class CorpusManifestTests(unittest.TestCase):
    def _git(self, cwd: Path, *args: str) -> None:
        subprocess.run(
            ["git", *args],
            cwd=cwd,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def _make_fixture(self, root: Path) -> tuple[Path, Path, Path]:
        translator = root / "projects" / "ESP32_PRJ_TO_LLVM"
        translator.mkdir(parents=True)
        self._git(root, "init", "-b", "ESPB_WORK")
        self._git(root, "config", "user.name", "ESPB host tests")
        self._git(root, "config", "user.email", "espb-tests@example.invalid")

        self._git(translator, "init", "-b", "test-translator")
        self._git(translator, "config", "user.name", "ESPB host tests")
        self._git(translator, "config", "user.email", "espb-tests@example.invalid")
        (translator / "translator.txt").write_text("translator fixture\n", encoding="utf-8")
        self._git(translator, "add", "translator.txt")
        self._git(translator, "commit", "-m", "translator fixture")

        source_dir = root / "tests" / "bytecode_groups"
        source_dir.mkdir(parents=True)
        (source_dir / "01_basic.c").write_text("int app(void) { return 42; }\n", encoding="utf-8")

        bitcode_dir = root / ".work" / "bytecode_groups"
        paired_dir = root / ".work" / "paired"
        bitcode_dir.mkdir(parents=True)
        paired_dir.mkdir(parents=True)
        (bitcode_dir / "01_basic.bc").write_bytes(b"bitcode fixture")
        (paired_dir / "01_basic.jit.espb").write_bytes(b"jit fixture")
        (paired_dir / "01_basic.interpreter.espb").write_bytes(b"interpreter fixture")

        self._git(root, "add", "tests/bytecode_groups/01_basic.c")
        self._git(root, "add", "projects/ESP32_PRJ_TO_LLVM")
        self._git(root, "commit", "-m", "corpus fixture")
        return bitcode_dir, paired_dir, source_dir

    def test_manifest_binds_source_translator_bitcode_and_both_variants(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bitcode_dir, paired_dir, _ = self._make_fixture(root)
            manifest = create_manifest(
                root, bitcode_dir, paired_dir, "smersh1307n2/ESPB", "ESPB_WORK", 12345
            )
            output = paired_dir / "manifest.json"
            import json

            output.write_text(json.dumps(manifest), encoding="utf-8")
            verified = verify_manifest(
                root,
                bitcode_dir,
                paired_dir,
                output,
                "smersh1307n2/ESPB",
                "ESPB_WORK",
                expected_run_id=12345,
                expected_groups=1,
                expected_source_commit=manifest["source_commit"],
            )
            self.assertEqual(verified["producer_run_id"], 12345)
            self.assertEqual(verified["group_count"], 1)

    def test_manifest_rejects_changed_corpus_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bitcode_dir, paired_dir, source_dir = self._make_fixture(root)
            manifest = create_manifest(
                root, bitcode_dir, paired_dir, "smersh1307n2/ESPB", "ESPB_WORK", 12345
            )
            output = paired_dir / "manifest.json"
            import json

            output.write_text(json.dumps(manifest), encoding="utf-8")
            (source_dir / "01_basic.c").write_text("int app(void) { return 43; }\n", encoding="utf-8")
            with self.assertRaisesRegex(ManifestError, "Source SHA-256 mismatch"):
                verify_manifest(
                    root,
                    bitcode_dir,
                    paired_dir,
                    output,
                    "smersh1307n2/ESPB",
                    "ESPB_WORK",
                )

    def test_manifest_rejects_producer_run_sha_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bitcode_dir, paired_dir, _ = self._make_fixture(root)
            manifest = create_manifest(
                root, bitcode_dir, paired_dir, "smersh1307n2/ESPB", "ESPB_WORK", 12345
            )
            output = paired_dir / "manifest.json"
            import json

            output.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ManifestError, "Producer workflow SHA mismatch"):
                verify_manifest(
                    root,
                    bitcode_dir,
                    paired_dir,
                    output,
                    "smersh1307n2/ESPB",
                    "ESPB_WORK",
                    expected_source_commit="0000000000000000000000000000000000000000",
                )

    def test_manifest_rejects_wrong_producer_run_id(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bitcode_dir, paired_dir, _ = self._make_fixture(root)
            manifest = create_manifest(
                root, bitcode_dir, paired_dir, "smersh1307n2/ESPB", "ESPB_WORK", 12345
            )
            output = paired_dir / "manifest.json"
            import json

            output.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ManifestError, "Workflow run mismatch"):
                verify_manifest(
                    root,
                    bitcode_dir,
                    paired_dir,
                    output,
                    "smersh1307n2/ESPB",
                    "ESPB_WORK",
                    expected_run_id=12346,
                )



class PairedReuseTests(unittest.TestCase):
    def _write(self, path: Path, data: bytes) -> str:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        import hashlib
        return hashlib.sha256(data).hexdigest()

    def test_reuses_only_modules_with_identical_bitcode_hash(self) -> None:
        import json

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            previous = root / "previous"
            bitcode = root / "current_bc"
            paired = root / "new_paired"
            bitcode.mkdir()

            old_a_bc = b"unchanged bitcode A"
            old_b_bc = b"old bitcode B"
            a_jit = b"JIT A"
            a_interp = b"Interpreter A"
            b_jit = b"JIT B"
            b_interp = b"Interpreter B"

            self._write(previous / "bytecode_groups" / "01_a.bc", old_a_bc)
            self._write(previous / "bytecode_groups" / "02_b.bc", old_b_bc)
            a_jit_hash = self._write(previous / "paired" / "01_a.jit.espb", a_jit)
            a_interp_hash = self._write(previous / "paired" / "01_a.interpreter.espb", a_interp)
            b_jit_hash = self._write(previous / "paired" / "02_b.jit.espb", b_jit)
            b_interp_hash = self._write(previous / "paired" / "02_b.interpreter.espb", b_interp)

            (previous / "paired" / "manifest.json").write_text(
                json.dumps({
                    "schema": "espb-paired-corpus/v1",
                    "repository": "smersh1307n2/ESPB",
                    "branch": "ESPB_WORK",
                    "translator_commit": "a" * 40,
                    "producer_run_id": 123,
                    "source_commit": "c" * 40,
                    "group_count": 2,
                    "groups": [
                        {
                            "name": "01_a",
                            "bitcode_sha256": self._write(
                                previous / "bytecode_groups" / "01_a.bc", old_a_bc
                            ),
                            "jit_path": ".work/paired/01_a.jit.espb",
                            "jit_sha256": a_jit_hash,
                            "interpreter_path": ".work/paired/01_a.interpreter.espb",
                            "interpreter_sha256": a_interp_hash,
                        },
                        {
                            "name": "02_b",
                            "bitcode_sha256": self._write(
                                previous / "bytecode_groups" / "02_b.bc", old_b_bc
                            ),
                            "jit_path": ".work/paired/02_b.jit.espb",
                            "jit_sha256": b_jit_hash,
                            "interpreter_path": ".work/paired/02_b.interpreter.espb",
                            "interpreter_sha256": b_interp_hash,
                        },
                    ],
                }),
                encoding="utf-8",
            )

            (bitcode / "01_a.bc").write_bytes(old_a_bc)
            (bitcode / "02_b.bc").write_bytes(b"new bitcode B")

            reused, changed = reuse_unchanged_pairs(
                previous, bitcode, paired,
                "smersh1307n2/ESPB", "ESPB_WORK", "a" * 40, 123, "c" * 40
            )

            self.assertEqual(reused, ["01_a"])
            self.assertEqual(changed, ["02_b"])
            self.assertEqual((paired / "01_a.jit.espb").read_bytes(), a_jit)
            self.assertEqual((paired / "01_a.interpreter.espb").read_bytes(), a_interp)
            self.assertFalse((paired / "02_b.jit.espb").exists())
            self.assertFalse((paired / "02_b.interpreter.espb").exists())

    def test_rejects_previous_translator_mismatch(self) -> None:
        import json

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            previous = root / "previous"
            bitcode = root / "current_bc"
            paired = root / "new_paired"
            bitcode.mkdir()
            (bitcode / "01_a.bc").write_bytes(b"bc")
            (previous / "paired").mkdir(parents=True)
            (previous / "paired" / "manifest.json").write_text(
                json.dumps({
                    "schema": "espb-paired-corpus/v1",
                    "repository": "smersh1307n2/ESPB",
                    "branch": "ESPB_WORK",
                    "translator_commit": "b" * 40,
                    "producer_run_id": 456,
                    "source_commit": "d" * 40,
                    "group_count": 0,
                    "groups": [],
                }),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ReuseError, "translator submodule mismatch"):
                reuse_unchanged_pairs(
                    previous, bitcode, paired,
                    "smersh1307n2/ESPB", "ESPB_WORK", "a" * 40, 123, "c" * 40
                )

    def test_rejects_corrupt_previous_module_hash(self) -> None:
        import json

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            previous = root / "previous"
            bitcode = root / "current_bc"
            paired = root / "new_paired"
            bitcode.mkdir()
            bc_hash = self._write(bitcode / "01_a.bc", b"same")
            self._write(previous / "paired" / "01_a.jit.espb", b"tampered jit")
            interp_hash = self._write(previous / "paired" / "01_a.interpreter.espb", b"interp")
            (previous / "paired" / "manifest.json").write_text(
                json.dumps({
                    "schema": "espb-paired-corpus/v1",
                    "repository": "smersh1307n2/ESPB",
                    "branch": "ESPB_WORK",
                    "translator_commit": "a" * 40,
                    "producer_run_id": 123,
                    "source_commit": "c" * 40,
                    "group_count": 1,
                    "groups": [{
                        "name": "01_a",
                        "bitcode_sha256": bc_hash,
                        "jit_path": "paired/01_a.jit.espb",
                        "jit_sha256": "0" * 64,
                        "interpreter_path": "paired/01_a.interpreter.espb",
                        "interpreter_sha256": interp_hash,
                    }],
                }),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ReuseError, "JIT module SHA-256 mismatch"):
                reuse_unchanged_pairs(
                    previous, bitcode, paired,
                    "smersh1307n2/ESPB", "ESPB_WORK", "a" * 40, 123, "c" * 40
                )


class EmittedOpcodeAuditTests(unittest.TestCase):
    @staticmethod
    def _make_audit_fixture(function_flags: tuple[int, ...]) -> bytes:
        code = bytearray(struct.pack("<I", len(function_flags)))
        instructions = bytes([0x11, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00])
        for flags in function_flags:
            body = struct.pack("<BBHHH", flags, 1, 0, 2, 0) + instructions
            code += struct.pack("<I", len(body)) + body
        section_offset = HEADER.size + SECTION.size
        file_header = HEADER.pack(0x42505345, 1, 0, 0, 1)
        section = SECTION.pack(CODE_SECTION_ID, 0, 0, section_offset, len(code))
        return file_header + section + code

    def test_decoder_counts_primary_extended_and_variable_length_instructions(self) -> None:
        code = bytes([
            0xFC, 0x08, 0x00, 0x12,                 # TABLE.SIZE, 2 operands
            0x09, 0x00, 0x00, 0xAA, 0x02, 0x0B, 0x05,  # typed CALL_IMPORT, 2 args
            0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  # BR_TABLE
            0x00,                                   # NOP
        ])
        primary, extended, instructions = decode_instruction_stream(code)
        self.assertEqual(primary[0xFC], 1)
        self.assertEqual(primary[0x09], 1)
        self.assertEqual(primary[0x04], 1)
        self.assertEqual(primary[0x00], 1)
        self.assertEqual(extended[0x08], 1)
        self.assertEqual(len(instructions), 4)

    def test_decoder_rejects_unknown_and_truncated_instructions(self) -> None:
        with self.assertRaises(AuditError):
            decode_instruction_stream(bytes([0xFE]))
        with self.assertRaises(AuditError):
            decode_instruction_stream(bytes([0x04, 0x00, 0x02, 0x00]))

    def test_interpreter_twin_may_differ_only_by_hot_flags(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            jit_path = root / "test.jit.espb"
            interp_path = root / "test.interpreter.espb"
            jit_path.write_bytes(self._make_audit_fixture((HOT, HOT)))
            interp_path.write_bytes(self._make_audit_fixture((0, 0)))

            from audit_espb_opcodes import audit_module
            jit = audit_module(jit_path)
            verify_interpreter_twin(jit, interp_path)

            corrupt = bytearray(interp_path.read_bytes())
            corrupt[-1] ^= 0x01
            interp_path.write_bytes(corrupt)
            with self.assertRaises(AuditError):
                verify_interpreter_twin(jit, interp_path)

    def test_directory_audit_checks_ten_pairs_and_dispatch_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for index in range(10):
                stem = f"{index:02d}_group"
                (root / f"{stem}.jit.espb").write_bytes(self._make_audit_fixture((HOT, HOT)))
                (root / f"{stem}.interpreter.espb").write_bytes(self._make_audit_fixture((0, 0)))
            dispatch = root / "runtime.c"
            dispatch.write_text(
                "dispatch_table[0x11] = &&op_0x11;\n"
                "dispatch_table[0x44] = &&op_0x44;\n",
                encoding="utf-8",
            )
            report = audit_directory(root, dispatch, expected_groups=10)
            self.assertEqual(report["group_count"], 10)
            self.assertEqual(report["function_count"], 20)
            self.assertEqual(report["primary_instruction_count"], 40)
            self.assertEqual(report["unique_primary_bytes"], ["0x11", "0x44"])
            self.assertEqual(report["interpreter_supported_not_emitted"], [])
            self.assertEqual(report["extended_supported_count"], 14)
            self.assertEqual(len(report["extended_supported_not_emitted"]), 14)
            self.assertIn("Extended handlers not emitted", render_markdown(report))
            self.assertNotIn(r"\\n", render_markdown(report))


class TranslatorKeyPoolTests(unittest.TestCase):
    def test_aggregate_and_numbered_keys_are_merged_without_duplicates(self) -> None:
        keys = collect_translator_keys({
            "ESPB_TRANSLATOR_KEYS": "aggregate-one\naggregate-two\n",
            "ESPB_API_KEY": "aggregate-one",
            "ESPB_API_KEY_1": "numbered-one",
            "ESPB_API_KEY_7": "numbered-seven",
            "ESPB_API_KEY_8": "numbered-eight",
            "ESPB_API_KEY_9": "numbered-nine",
        })
        self.assertEqual(
            keys,
            [
                "aggregate-one",
                "aggregate-two",
                "numbered-one",
                "numbered-seven",
                "numbered-eight",
                "numbered-nine",
            ],
        )


class CompilerWrapperTests(unittest.TestCase):
    def test_rv32_a_extension_is_scoped_to_atomic_group_only(self) -> None:
        base = [
            "--target=riscv32-esp-elf",
            "-march=rv32imc_zicsr_zifencei",
            "/tmp/08_atomics.c",
            "-emit-llvm",
        ]
        adjusted, applied = adjust_clang_args(base)
        self.assertIn(ATOMIC_MARCH, adjusted)
        self.assertIn(ATOMIC_MARCH, applied)
        self.assertNotIn("-march=rv32imc_zicsr_zifencei", adjusted)
        self.assertEqual(sum(arg.startswith("-march=") for arg in adjusted), 1)

        float_source = [
            "--target=riscv32-esp-elf",
            "-march=rv32imc_zicsr_zifencei",
            "/tmp/03_float.c",
            "-emit-llvm",
        ]
        float_adjusted, float_applied = adjust_clang_args(float_source)
        self.assertTrue(all(option in float_adjusted for option in FLOAT_OPTIONS))
        self.assertTrue(all(option in float_applied for option in FLOAT_OPTIONS))
        self.assertIn("-march=rv32imc_zicsr_zifencei", float_adjusted)

        normal = [
            "--target=riscv32-esp-elf",
            "-march=rv32imc_zicsr_zifencei",
            "/tmp/07_calls_callbacks.c",
            "-emit-llvm",
        ]
        unchanged, applied = adjust_clang_args(normal)
        self.assertFalse(applied)
        self.assertEqual(unchanged, normal)


class WorkflowContractTests(unittest.TestCase):
    def test_qemu_selects_successful_provenance_bound_pair_run(self) -> None:
        workflow = (REPO_ROOT / ".github" / "workflows" / "espb-qemu.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("build_opcode_groups_espb.yml", workflow)
        self.assertIn('run.get("status") == "completed"', workflow)
        self.assertIn('run.get("conclusion") == "success"', workflow)
        self.assertIn('run.get("path") == ".github/workflows/build_opcode_groups_espb.yml"', workflow)
        self.assertIn('item.get("name") == "espb-opcode-groups-paired"', workflow)
        self.assertIn("run-id: ${{ steps.paired_run.outputs.run_id }}", workflow)
        self.assertIn('--expected-run-id "$PAIRED_RUN_ID"', workflow)
        self.assertIn('--expected-source-commit "$PAIRED_SOURCE_SHA"', workflow)
        self.assertIn("--expected-groups 10", workflow)
        self.assertNotIn("status=success", workflow)
        self.assertNotIn("44861542751", workflow)
        self.assertNotIn("outputs.run_id }}}", workflow)
        self.assertEqual(
            workflow.count("PAIRED_RUN_ID: ${{ steps.paired_run.outputs.run_id }}"),
            1,
        )
        self.assertEqual(
            workflow.count("PAIRED_SOURCE_SHA: ${{ steps.paired_run.outputs.source_sha }}"),
            1,
        )

    def test_translator_reuses_only_matching_paired_content(self) -> None:
        workflow = (
            REPO_ROOT / ".github" / "workflows" / "build_opcode_groups_espb.yml"
        ).read_text(encoding="utf-8")
        helper = (
            REPO_ROOT / ".github" / "scripts" / "reuse_paired_groups.py"
        ).read_text(encoding="utf-8")

        self.assertIn("previous_pair_run_id=", workflow)
        self.assertIn("previous_pair_source_sha", workflow)
        self.assertIn("PREVIOUS_PAIR_RUN_ID: ${{ steps.bitcode.outputs.previous_pair_run_id }}", workflow)
        self.assertIn("PREVIOUS_PAIR_SOURCE_SHA: ${{ steps.bitcode.outputs.previous_pair_source_sha }}", workflow)
        self.assertIn('--expected-run-id "$PREVIOUS_PAIR_RUN_ID"', workflow)
        self.assertIn('--expected-source-commit "$PREVIOUS_PAIR_SOURCE_SHA"', workflow)
        self.assertIn("actions/download-artifact@v4", workflow)
        self.assertIn("Reuse unchanged paired modules by bitcode SHA-256", workflow)
        self.assertIn("TRANSLATION_REUSED group=$stem", workflow)
        self.assertIn("TRANSLATION_REQUIRED group=$stem", workflow)
        self.assertIn('current_hash != old.get("bitcode_sha256")', helper)
        self.assertIn('manifest.get("producer_run_id") != expected_run_id', helper)
        self.assertIn('manifest.get("source_commit") != expected_source_commit', helper)
        self.assertIn('sha256(old_jit) != jit_hash', helper)
        self.assertIn('sha256(old_interpreter) != interpreter_hash', helper)


    def test_interpreter_reserves_and_rewinds_root_shadow_stack_frame(self) -> None:
        runtime = (
            REPO_ROOT / "components" / "espb" / "src" / "espb_interpreter_runtime_oc.c"
        ).read_text(encoding="utf-8")

        self.assertIn("const bool is_root_call = (exec_ctx->call_stack_top == 0);", runtime)
        self.assertIn("const int entry_saved_call_stack_top = exec_ctx->call_stack_top;", runtime)
        self.assertIn("const size_t entry_saved_sp = exec_ctx->sp;", runtime)
        self.assertIn("const size_t frame_base = is_root_call ? entry_saved_sp : exec_ctx->sp;", runtime)
        self.assertIn("exec_ctx->sp = frame_base + frame_size_bytes;", runtime)
        self.assertIn("exec_ctx->sp = entry_saved_sp;", runtime)
        self.assertIn("exec_ctx->fp = entry_saved_fp;", runtime)
        self.assertIn("exec_ctx->call_stack_top = entry_saved_call_stack_top;", runtime)


    def test_qemu_executes_atomic_jit_on_both_architectures(self) -> None:
        workflow = (REPO_ROOT / ".github" / "workflows" / "espb-qemu.yml").read_text(
            encoding="utf-8"
        )
        harness = (REPO_ROOT / "qemu" / "main" / "main.cpp").read_text(
            encoding="utf-8"
        )
        xtensa_backend = (
            REPO_ROOT / "components" / "espb" / "src" / "arch" / "jit_xtensa_inline.c"
        ).read_text(encoding="utf-8")
        interpreter_runtime = (
            REPO_ROOT / "components" / "espb" / "src" / "espb_interpreter_runtime_oc.c"
        ).read_text(encoding="utf-8")
        jit_indirect_helper = (
            REPO_ROOT / "components" / "espb" / "src" / "espb_jit_indirect_ptr.c"
        ).read_text(encoding="utf-8")
        component_cmake = (REPO_ROOT / "components" / "espb" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )

        self.assertIn('{"host_invoke_cb", (const void*)&host_invoke_cb}', harness)
        self.assertIn('{"host_invoke_cb2", (const void*)&host_invoke_cb2}', harness)
        self.assertIn('{"__atomic_exchange_8", (const void*)&qemu_atomic_exchange_8}', harness)
        self.assertIn('case 0xD7:', xtensa_backend)
        self.assertIn('case 0xDD:', xtensa_backend)
        self.assertIn('case 0xEC:', xtensa_backend)
        self.assertIn('case 0xF6:', xtensa_backend)
        self.assertIn('"src/arch/jit_xtensa_inline.c"', component_cmake)
        self.assertIn('pointer_value > 0 && pointer_value < instance->memory_size_bytes', interpreter_runtime)
        self.assertIn('raw_ptr > 0 && raw_ptr < instance->memory_size_bytes', jit_indirect_helper)

        self.assertNotIn("jit_skipped_unsupported", harness)
        self.assertNotIn("jit_skipped_count", workflow)
        self.assertIn('group_passes" -lt 2', workflow)
        self.assertIn("benchmark_modules=$((expected_modules - 4))", workflow)
        self.assertIn("expected_bench_jit=$((jit_modules - 2))", workflow)
        self.assertIn('if [ "$memory_count" -ne "$expected_modules" ]', workflow)
        self.assertIn('git config --global --add safe.directory "$GITHUB_WORKSPACE"', workflow)


    def test_translator_key_pool_includes_slots_one_through_nine(self) -> None:
        script = (REPO_ROOT / ".github" / "scripts" / "translate_bc.py").read_text(
            encoding="utf-8"
        )
        workflow = (
            REPO_ROOT / ".github" / "workflows" / "build_opcode_groups_espb.yml"
        ).read_text(encoding="utf-8")

        self.assertIn('range(1, 10)', script)
        self.assertIn('"ESPB_API_KEY"', script)
        self.assertIn("ESPB_API_KEY_9", script)
        for index in range(1, 10):
            name = f"ESPB_API_KEY_{index}"
            self.assertIn(name, workflow)
        self.assertIn("through ESPB_API_KEY_9", script)


if __name__ == "__main__":
    unittest.main()
