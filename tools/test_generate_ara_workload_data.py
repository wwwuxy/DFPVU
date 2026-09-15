#!/usr/bin/env python3
"""Integration contract for the DFPVU trace-to-ARA data generator."""

import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "generate_ara_workload_data.py"


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="ara-workload-data-") as temporary:
        header = pathlib.Path(temporary) / "generated_data.h"
        completed = subprocess.run(
            [
                sys.executable,
                str(GENERATOR),
                "--trace",
                str(ROOT / "test_src" / "qwen3-p32-fixture"),
                "--add-trace",
                str(ROOT / "test_src" / "llm-p32-add-fixture"),
                "--output",
                str(header),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 0, completed.stderr
        text = header.read_text(encoding="utf-8")
        assert '#define ARA_PROFILE "legacy-qwen3-v1"' in text
        assert "ARA_CASE_MAC" in text
        assert "ARA_CASE_DOT" in text
        assert "ARA_CASE_MUL" in text
        assert "ARA_CASE_TO_INT" in text
        assert "ARA_CASE_ADD" in text
        assert "source_elements" not in text
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
