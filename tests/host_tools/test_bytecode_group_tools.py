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

    def test_qemu_accounts_for_xtensa_atomic_jit_skip(self) -> None:
        workflow = (REPO_ROOT / ".github" / "workflows" / "espb-qemu.yml").read_text(
            encoding="utf-8"
        )
        harness = (REPO_ROOT / "qemu" / "main" / "main.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("jit_skipped_unsupported=", harness)
        self.assertIn("xtensa_jit_has_no_atomic_handlers", harness)
        self.assertIn('{"host_invoke_cb", (const void*)&host_invoke_cb}', harness)
        self.assertIn('{"host_invoke_cb2", (const void*)&host_invoke_cb2}', harness)
        self.assertIn('{"__atomic_exchange_8", (const void*)&qemu_atomic_exchange_8}', harness)

        self.assertIn('expected_jit_skips=1', workflow)
        self.assertIn('expected_jit_skips=0', workflow)
        self.assertIn("expected_jit_ready=$((jit_modules - jit_skipped_count))", workflow)
        self.assertIn('expected_group_passes=1', workflow)
        self.assertIn("benchmark_modules=$((expected_modules - 4 - jit_skipped_count))", workflow)
        self.assertIn("expected_bench_jit=$((jit_modules - 2 - jit_skipped_count))", workflow)
        self.assertIn("expected_memory_count=$((expected_modules - jit_skipped_count))", workflow)
        self.assertEqual(workflow.count("interp_ready_count="), 1)
        self.assertEqual(workflow.count("expected_jit_ready="), 1)
        self.assertEqual(workflow.count("      - name: Upload QEMU execution log"), 1)
        self.assertEqual(workflow.count("          retention-days: 14"), 1)

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
