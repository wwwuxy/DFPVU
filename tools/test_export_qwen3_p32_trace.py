import unittest


class WriteTensorTests(unittest.TestCase):
    def test_write_tensor_is_little_endian_f32(self):
        import tempfile
        from pathlib import Path

        import numpy as np
        from export_qwen3_p32_trace import write_tensor

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "tensor.f32"
            write_tensor(path, np.array([[1.5, -2.0]], dtype=np.float32))
            self.assertEqual(
                path.read_bytes(), bytes.fromhex("0000c03f000000c0")
            )
