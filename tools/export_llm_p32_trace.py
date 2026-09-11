#!/usr/bin/env python3
"""Export arbitrary decoder-layer linear modules for Posit32 MAC replay."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


PROFILES = (
    "qwen3-0.6b",
    "gemma3-1b",
    "phi4-mini",
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
        raise ValueError(
            f"expected a [tokens, features] or [rows, features] tensor, got {tuple(tensor.shape)}"
        )
    return tensor.detach().to(device="cpu", dtype=torch.float32).contiguous().numpy()


def _decoder_layer(model, layer_index: int):
    import torch

    candidates = (
        "model.layers",
        "layers",
        "model.language_model.layers",
        "language_model.layers",
    )
    for path in candidates:
        current = model
        try:
            for component in path.split("."):
                current = getattr(current, component)
        except AttributeError:
            continue
        if isinstance(current, torch.nn.ModuleList):
            if layer_index < 0 or layer_index >= len(current):
                raise ValueError(f"layer index {layer_index} is outside {path}")
            return current[layer_index]
    raise ValueError("cannot find a decoder layer list on this model")


def _is_traceable_linear(module) -> bool:
    import torch

    weight = getattr(module, "weight", None)
    return isinstance(weight, torch.Tensor) and weight.ndim == 2


def _artifact_prefix(index: int, module_path: str) -> str:
    normalized = "".join(
        character if character.isascii() and character.isalnum() else "_"
        for character in module_path
    ).strip("_")
    if not normalized:
        raise ValueError(f"cannot derive an artifact prefix from {module_path!r}")
    return f"{index:03d}_{normalized}"


def _selected_linear_modules(layer, requested_paths: list[str]):
    if requested_paths:
        selected = []
        for index, module_path in enumerate(requested_paths):
            module = layer.get_submodule(module_path)
            if not _is_traceable_linear(module):
                raise TypeError(f"{module_path} does not expose a rank-2 floating-point weight")
            selected.append((module_path, module, _artifact_prefix(index, module_path)))
        return selected

    selected = []
    for module_path, module in layer.named_modules():
        if not module_path or not _is_traceable_linear(module):
            continue
        selected.append((module_path, module, _artifact_prefix(len(selected), module_path)))
    if not selected:
        raise ValueError("decoder layer exposes no traceable rank-2 linear weights")
    return selected


def capture_linear_modules(model, layer_index: int, input_ids, requested_paths: list[str]):
    """Capture invoked linear modules and export a bias-free FP32 reference output."""
    import torch

    layer = _decoder_layer(model, layer_index)
    captures: dict[str, dict] = {}
    handles = []
    try:
        for module_path, module, artifact_prefix in _selected_linear_modules(
            layer, requested_paths
        ):

            def pre_hook(_module, inputs, name=module_path, prefix=artifact_prefix):
                if name in captures:
                    raise RuntimeError(f"{name} was invoked more than once")
                if not inputs:
                    raise RuntimeError(f"{name} has no tensor input")
                captures[name] = {
                    "name": name,
                    "artifact_prefix": prefix,
                    "input": _trace_matrix(inputs[0]),
                    "weight": _trace_matrix(_module.weight),
                    "has_bias": getattr(_module, "bias", None) is not None,
                }

            def forward_hook(_module, inputs, _output, name=module_path):
                if name not in captures:
                    raise RuntimeError(f"{name} output arrived before its input capture")
                with torch.no_grad():
                    reference = torch.nn.functional.linear(inputs[0], _module.weight, None)
                captures[name]["output"] = _trace_matrix(reference)

            handles.append(module.register_forward_pre_hook(pre_hook))
            handles.append(module.register_forward_hook(forward_hook))

        with torch.no_grad():
            model(input_ids=input_ids, use_cache=False)
    finally:
        for handle in handles:
            handle.remove()

    missing = [name for name, capture in captures.items() if "output" not in capture]
    if missing:
        raise RuntimeError(f"captured modules without output: {', '.join(missing)}")
    if not captures:
        raise RuntimeError("none of the selected linear modules were invoked")
    return [captures[name] for name in sorted(captures)]


def encode_token_ids(tokenizer, prompt: str, tokens: int) -> list[int]:
    """Encode exactly *tokens* deterministic IDs, repeating short prompts."""
    if tokens <= 0:
        raise ValueError("tokens must be positive")
    token_ids = tokenizer.encode(prompt, add_special_tokens=False)
    if not token_ids:
        if tokenizer.eos_token_id is None:
            raise ValueError("empty prompt requires a tokenizer EOS token")
        token_ids = [tokenizer.eos_token_id]
    return [int(token_ids[index % len(token_ids)]) for index in range(tokens)]


def write_trace(
    output_dir: Path,
    model_path: Path,
    profile: str,
    prompt: str,
    token_ids: list[int],
    layer_index: int,
    captures: list[dict],
) -> None:
    """Write a self-describing v2 trace with pre-bias linear references."""
    output_dir.mkdir(parents=True, exist_ok=True)
    descriptors = []
    for capture in captures:
        prefix = capture["artifact_prefix"]
        for tensor_name in ("input", "weight", "output"):
            write_tensor(output_dir / f"{prefix}.{tensor_name}.f32", capture[tensor_name])
        descriptors.append(
            {
                "name": capture["name"],
                "artifact_prefix": prefix,
                "input_shape": list(capture["input"].shape),
                "weight_shape": list(capture["weight"].shape),
                "output_shape": list(capture["output"].shape),
                "dtype": "float32-le",
                "has_bias": bool(capture["has_bias"]),
            }
        )

    metadata = {
        "format_version": 2,
        "profile": profile,
        "output_semantics": "linear-no-bias",
        "model": str(model_path),
        "prompt": prompt,
        "input_token_ids": [int(token_id) for token_id in token_ids],
        "layer_index": layer_index,
        "modules": descriptors,
    }
    (output_dir / "metadata.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=PROFILES, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prompt", default=DEFAULT_PROMPT)
    parser.add_argument("--tokens", type=int, default=16)
    parser.add_argument("--layer", type=int, default=0)
    parser.add_argument(
        "--module",
        action="append",
        default=[],
        help="relative decoder-layer path to export; repeat to select an explicit subset",
    )
    parser.add_argument("--trust-remote-code", action="store_true")
    arguments = parser.parse_args()

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer

    tokenizer = AutoTokenizer.from_pretrained(
        arguments.model, trust_remote_code=arguments.trust_remote_code
    )
    token_ids = encode_token_ids(tokenizer, arguments.prompt, arguments.tokens)
    input_ids = torch.tensor([token_ids], dtype=torch.long)
    model = AutoModelForCausalLM.from_pretrained(
        arguments.model,
        torch_dtype="auto",
        trust_remote_code=arguments.trust_remote_code,
    )
    model.eval()
    captures = capture_linear_modules(
        model, arguments.layer, input_ids, arguments.module
    )
    write_trace(
        arguments.output,
        arguments.model,
        arguments.profile,
        arguments.prompt,
        token_ids,
        arguments.layer,
        captures,
    )


if __name__ == "__main__":
    main()
