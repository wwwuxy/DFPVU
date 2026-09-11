#!/usr/bin/env python3
"""Compatibility wrapper for the Qwen3-0.6B LLM trace exporter."""

from __future__ import annotations

import sys

from export_llm_p32_trace import main, write_tensor


if __name__ == "__main__":
    if "--profile" not in sys.argv:
        sys.argv[1:1] = ["--profile", "qwen3-0.6b"]
    main()
