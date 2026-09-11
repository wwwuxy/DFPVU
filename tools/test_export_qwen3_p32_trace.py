import json
import tempfile
import unittest
from pathlib import Path

import numpy as np

from export_llm_p32_trace import write_tensor, write_trace


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
            )
            metadata = json.loads((root / "metadata.json").read_text())
            self.assertEqual(metadata["format_version"], 2)
            self.assertEqual(metadata["profile"], "phi4-mini")
            self.assertEqual(metadata["output_semantics"], "linear-no-bias")
            self.assertTrue(metadata["modules"][0]["has_bias"])
            self.assertEqual(metadata["modules"][0]["input_shape"], [1, 4])
            self.assertEqual(
                (root / "000_mlp_down_proj.output.f32").read_bytes(),
                bytes.fromhex("0000204100002041"),
            )


if __name__ == "__main__":
    unittest.main()
