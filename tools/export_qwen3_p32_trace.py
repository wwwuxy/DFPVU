#!/usr/bin/env python3
"""Export layer-0 Qwen3 linear projection tensors for P32 MAC replay."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


MODULES = (
    ("self_attn.q_proj", "q_proj"),
    ("self_attn.k_proj", "k_proj"),
    ("self_attn.v_proj", "v_proj"),
    ("mlp.gate_proj", "gate_proj"),
    ("mlp.up_proj", "up_proj"),
    ("mlp.down_proj", "down_proj"),
)
DEFAULT_PROMPT = "The quick brown fox jumps over the lazy dog."


def write_tensor(path: Path, tensor: np.ndarray) -> None:
    """Write a tensor as contiguous row-major little-endian float32 values."""
    data = np.ascontiguousarray(tensor, dtype="<f4")
    path.write_bytes(data.tobytes(order="C"))


def _trace_matrix(tensor):
    import torch

    if not isinstance(tensor, torch.Tensor):
        raise TypeError(f"expected a Tensor capture, got {type(tensor).__name__}")
    if tensor.ndim == 3 and tensor.shape[0] == 1:
        tensor = tensor[0]
    if tensor.ndim != 2:
        raise ValueError(f"expected a [tokens, features] tensor, got {tuple(tensor.shape)}")
    return tensor.detach().to(device="cpu", dtype=torch.float32).contiguous().numpy()


def capture_linear_inputs(model, layer_index: int, input_ids):
    """Capture the selected layer's linear inputs and outputs from one forward pass."""
    import torch

    base_model = getattr(model, "model", model)
    try:
        layer = base_model.layers[layer_index]
    except (AttributeError, IndexError, TypeError) as error:
        raise ValueError(f"cannot find decoder layer {layer_index}") from error

    captures = {}
    handles = []
    try:
        for module_path, artifact_prefix in MODULES:
            module = layer.get_submodule(module_path)
            if not isinstance(module, torch.nn.Linear):
                raise TypeError(f"{module_path} is not torch.nn.Linear")
            if module.bias is not None:
                raise ValueError(f"{module_path} has a bias, which this trace format rejects")

            def pre_hook(_module, inputs, name=module_path):
                if name in captures and "input" in captures[name]:
                    raise RuntimeError(f"{name} was invoked more than once")
                captures.setdefault(name, {})["input"] = _trace_matrix(inputs[0])

            def forward_hook(_module, _inputs, output, name=module_path):
                if name in captures and "output" in captures[name]:
                    raise RuntimeError(f"{name} was invoked more than once")
                captures.setdefault(name, {})["output"] = _trace_matrix(output)

            captures[module_path] = {
                "weight": _trace_matrix(module.weight),
                "artifact_prefix": artifact_prefix,
            }
            handles.append(module.register_forward_pre_hook(pre_hook))
            handles.append(module.register_forward_hook(forward_hook))

        with torch.no_grad():
            model(input_ids=input_ids, use_cache=False)
    finally:
        for handle in handles:
            handle.remove()

    missing = [
        module_path
        for module_path, _ in MODULES
        if module_path not in captures
        or "input" not in captures[module_path]
        or "output" not in captures[module_path]
    ]
    if missing:
        raise RuntimeError(f"did not capture modules: {', '.join(missing)}")
    return captures


def encode_token_ids(tokenizer, prompt: str, tokens: int) -> list[int]:
    """Encode exactly *tokens* deterministic IDs, repeating short prompts."""
    if tokens != 16:
        raise ValueError("Qwen3 P32 trace export requires exactly 16 tokens")
    token_ids = tokenizer.encode(prompt, add_special_tokens=False)
    if not token_ids:
        if tokenizer.eos_token_id is None:
            raise ValueError("empty prompt requires a tokenizer EOS token")
        token_ids = [tokenizer.eos_token_id]
    return [int(token_ids[index % len(token_ids)]) for index in range(tokens)]


def write_trace(output_dir: Path, model_path: Path, prompt: str, token_ids, captures) -> None:
    """Write captures and their format-version-1 metadata."""
    output_dir.mkdir(parents=True, exist_ok=True)
    descriptors = []
    for module_path, artifact_prefix in MODULES:
        capture = captures[module_path]
        for tensor_name in ("input", "weight", "output"):
            write_tensor(
                output_dir / f"{artifact_prefix}.{tensor_name}.f32",
                capture[tensor_name],
            )
        descriptors.append(
            {
                "name": module_path,
                "artifact_prefix": artifact_prefix,
                "input_shape": list(capture["input"].shape),
                "weight_shape": list(capture["weight"].shape),
                "output_shape": list(capture["output"].shape),
                "dtype": "float32-le",
                "has_bias": False,
            }
        )

    metadata = {
        "format_version": 1,
        "model": str(model_path),
        "prompt": prompt,
        "input_token_ids": [int(token_id) for token_id in token_ids],
        "layer_index": 0,
        "modules": descriptors,
    }
    (output_dir / "metadata.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prompt", default=DEFAULT_PROMPT)
    parser.add_argument("--tokens", type=int, default=16)
    arguments = parser.parse_args()

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer

    token_ids = encode_token_ids(
        AutoTokenizer.from_pretrained(arguments.model), arguments.prompt, arguments.tokens
    )
    input_ids = torch.tensor([token_ids], dtype=torch.long)
    model = AutoModelForCausalLM.from_pretrained(arguments.model, torch_dtype="auto")
    model.eval()
    captures = capture_linear_inputs(model, 0, input_ids)
    write_trace(arguments.output, arguments.model, arguments.prompt, token_ids, captures)


if __name__ == "__main__":
    main()
