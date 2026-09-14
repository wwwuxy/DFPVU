import json
import tempfile
import unittest
from pathlib import Path

import numpy as np
import export_llm_p32_trace as exporter

from export_llm_p32_trace import capture_linear_modules, write_tensor, write_trace


class LlmTraceExportTests(unittest.TestCase):
    def test_write_tensor_is_little_endian_f32(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "tensor.f32"
            write_tensor(path, np.array([[1.5, -2.0]], dtype=np.float32))
            self.assertEqual(
                path.read_bytes(), bytes.fromhex("0000c03f000000c0")
            )

    def test_write_trace_uses_generic_v2_pre_bias_contract(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            captures = [
                {
                    "name": "mlp.down_proj",
                    "artifact_prefix": "000_mlp_down_proj",
                    "input": np.array([[1.0, 2.0, 3.0, 4.0]], dtype=np.float32),
                    "weight": np.ones((2, 4), dtype=np.float32),
                    "output": np.array([[10.0, 10.0]], dtype=np.float32),
                    "has_bias": True,
                }
            ]
            write_trace(
                root,
                Path("/root/models/phi-4-mini-instruct"),
                "phi4-mini",
                "fixture prompt",
                [7],
                3,
                captures,
                linear_module_scope="explicit-subset",
            )
            metadata = json.loads((root / "metadata.json").read_text())
            self.assertEqual(metadata["format_version"], 2)
            self.assertEqual(metadata["profile"], "phi4-mini")
            self.assertEqual(metadata["output_semantics"], "linear-no-bias")
            self.assertEqual(metadata["linear_module_scope"], "explicit-subset")
            self.assertTrue(metadata["modules"][0]["has_bias"])
            self.assertEqual(metadata["modules"][0]["input_shape"], [1, 4])
            self.assertEqual(
                (root / "000_mlp_down_proj.output.f32").read_bytes(),
                bytes.fromhex("0000204100002041"),
            )


    def test_capture_empty_selection_discovers_all_decoder_linears_and_writes_scope(self):
        import torch

        class ToySelfAttention(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.q_proj = torch.nn.Linear(2, 2, bias=False)
                self.k_proj = torch.nn.Linear(2, 2, bias=False)
                self.v_proj = torch.nn.Linear(2, 2, bias=False)
                self.o_proj = torch.nn.Linear(2, 2, bias=False)

            def forward(self, hidden):
                return self.o_proj(
                    self.v_proj(self.k_proj(self.q_proj(hidden)))
                )

        class ToyMlp(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.gate_proj = torch.nn.Linear(2, 2, bias=False)
                self.up_proj = torch.nn.Linear(2, 2, bias=False)
                self.down_proj = torch.nn.Linear(2, 2, bias=False)

            def forward(self, hidden):
                return self.down_proj(self.up_proj(self.gate_proj(hidden)))

        class ToyLayer(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.self_attn = ToySelfAttention()
                self.mlp = ToyMlp()

            def forward(self, input_ids):
                hidden = input_ids.to(dtype=torch.float32)
                return self.mlp(self.self_attn(hidden))

        class ToyContainer(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.layers = torch.nn.ModuleList([ToyLayer()])

        class ToyModel(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.model = ToyContainer()

            def forward(self, input_ids, use_cache):
                del use_cache
                return self.model.layers[0](input_ids)

        model = ToyModel()
        token_ids = torch.tensor([[1, 2]], dtype=torch.long)
        captures = capture_linear_modules(model, 0, token_ids, [])
        self.assertEqual(
            [capture["name"] for capture in captures],
            [
                "mlp.down_proj",
                "mlp.gate_proj",
                "mlp.up_proj",
                "self_attn.k_proj",
                "self_attn.o_proj",
                "self_attn.q_proj",
                "self_attn.v_proj",
            ],
        )

        explicit_captures = capture_linear_modules(
            model, 0, token_ids, ["self_attn.q_proj"]
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            write_trace(
                root / "all",
                Path("/tmp/model"),
                "gemma3-1b",
                "fixture",
                [7],
                0,
                captures,
                linear_module_scope="all-decoder-linear",
            )
            metadata = json.loads((root / "all" / "metadata.json").read_text())
            self.assertEqual(metadata["linear_module_scope"], "all-decoder-linear")

            write_trace(
                root / "explicit",
                Path("/tmp/model"),
                "gemma3-1b",
                "fixture",
                [7],
                0,
                explicit_captures,
                linear_module_scope="explicit-subset",
            )
            metadata = json.loads((root / "explicit" / "metadata.json").read_text())
            self.assertEqual(metadata["linear_module_scope"], "explicit-subset")

    def test_write_trace_rejects_unknown_linear_module_scope(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaisesRegex(ValueError, "linear_module_scope"):
                write_trace(
                    Path(temporary_directory),
                    Path("/tmp/model"),
                    "gemma3-1b",
                    "fixture",
                    [7],
                    0,
                    [],
                    linear_module_scope="one-linear-module",
                )

    def test_write_trace_records_add_operands_and_output(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            add_captures = [
                {
                    "name": "add.000",
                    "artifact_prefix": "000_add",
                    "lhs": np.array([[1.0, -2.0]], dtype=np.float32),
                    "rhs": np.array([[3.0, 4.0]], dtype=np.float32),
                    "output": np.array([[4.0, 2.0]], dtype=np.float32),
                }
            ]
            write_trace(
                root,
                Path("/root/models/phi-4-mini-instruct"),
                "phi4-mini",
                "fixture prompt",
                [7],
                3,
                [],
                add_captures,
                linear_module_scope="all-decoder-linear",
            )
            metadata = json.loads((root / "metadata.json").read_text())
            self.assertEqual(metadata["add_operations"][0]["name"], "add.000")
            self.assertEqual(metadata["add_operations"][0]["shape"], [1, 2])
            self.assertEqual(
                (root / "000_add.lhs.f32").read_bytes(),
                bytes.fromhex("0000803f000000c0"),
            )
            self.assertEqual(
                (root / "000_add.output.f32").read_bytes(),
                bytes.fromhex("0000804000000040"),
            )

    def test_capture_linear_modules_records_real_binary_add(self):
        import torch

        class ToyLayer(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.proj = torch.nn.Linear(2, 2, bias=False)
                with torch.no_grad():
                    self.proj.weight.copy_(torch.eye(2))

            def forward(self, input_ids):
                hidden = input_ids.to(dtype=torch.float32)
                return torch.add(hidden, self.proj(hidden))

        class ToyContainer(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.layers = torch.nn.ModuleList([ToyLayer()])

        class ToyModel(torch.nn.Module):
            def __init__(self):
                super().__init__()
                self.model = ToyContainer()

            def forward(self, input_ids, use_cache):
                del use_cache
                return self.model.layers[0](input_ids)

        add_captures = []
        captures = capture_linear_modules(
            ToyModel(),
            0,
            torch.tensor([[1, 2]], dtype=torch.long),
            ["proj"],
            add_captures=add_captures,
        )
        self.assertEqual(len(captures), 1)
        self.assertEqual(len(add_captures), 1)
        self.assertTrue(
            np.array_equal(add_captures[0]["lhs"], np.array([[1.0, 2.0]]))
        )
        self.assertTrue(
            np.array_equal(add_captures[0]["rhs"], np.array([[1.0, 2.0]]))
        )
        self.assertTrue(
            np.array_equal(add_captures[0]["output"], np.array([[2.0, 4.0]]))
        )

    def test_cli_exposes_capture_add_option(self):
        import subprocess
        import sys

        result = subprocess.run(
            [sys.executable, str(Path(__file__).with_name("export_llm_p32_trace.py")), "--help"],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("--capture-add", result.stdout)

    def test_capture_add_cli_passes_linear_then_add_captures_to_writer(self):
        import sys
        import torch
        import types
        from unittest import mock

        class FakeTokenizer:
            eos_token_id = 0

            def encode(self, prompt, add_special_tokens):
                del prompt, add_special_tokens
                return [7]

        class FakeModel:
            def eval(self):
                return self

        fake_transformers = types.SimpleNamespace(
            AutoTokenizer=types.SimpleNamespace(
                from_pretrained=lambda *args, **kwargs: FakeTokenizer()
            ),
            AutoModelForCausalLM=types.SimpleNamespace(
                from_pretrained=lambda *args, **kwargs: FakeModel()
            ),
        )
        linear_captures = [{"name": "linear"}]
        add_capture = {"name": "add.000"}

        def fake_capture(*args, add_captures):
            del args
            self.assertIsNotNone(add_captures)
            add_captures.append(add_capture)
            return linear_captures

        with mock.patch.object(
            sys,
            "argv",
            [
                "export_llm_p32_trace.py",
                "--profile",
                "gemma3-1b",
                "--model",
                "/tmp/model",
                "--output",
                "/tmp/trace",
                "--capture-add",
            ],
        ), mock.patch.dict(sys.modules, {"transformers": fake_transformers}), mock.patch.object(
            exporter, "capture_linear_modules", side_effect=fake_capture
        ), mock.patch.object(exporter, "write_trace") as write:
            exporter.main()

        arguments = write.call_args.args
        self.assertIs(arguments[6], linear_captures)
        self.assertEqual(arguments[7], [add_capture])
        self.assertEqual(
            write.call_args.kwargs["linear_module_scope"], "all-decoder-linear"
        )

if __name__ == "__main__":
    unittest.main()
