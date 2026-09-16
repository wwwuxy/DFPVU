#!/usr/bin/env python3
"""Select DFPVU workload operands and emit a bounded ARA C header.

Only finite IEEE-754 binary32 values are accepted.  The selection order and
clamping rules mirror the DFPVU workload helpers: modules are keyed by
artifact prefix, scalar elements and four-element vectors are evenly sampled,
and the MAC workload uses the first M/K coordinates and evenly spaced rows.
"""

import argparse
import json
import math
import pathlib
import struct
import sys
from typing import Dict, Iterable, List, Sequence, Tuple


SAMPLE_COUNT = 256
MAC_M = 16
MAC_N = 16
MAC_K = 16
DOT_WIDTH = 4


def f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def f32_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def require_shape(shape: object, context: str) -> Tuple[int, int]:
    if not isinstance(shape, list) or len(shape) != 2:
        raise ValueError(f"{context}: shape must contain two dimensions")
    if any(not isinstance(dimension, int) or dimension <= 0 for dimension in shape):
        raise ValueError(f"{context}: shape dimensions must be positive integers")
    return shape[0], shape[1]


def read_tensor(root: pathlib.Path, filename: str, shape: Tuple[int, int]) -> List[float]:
    path = root / filename
    data = path.read_bytes()
    expected_bytes = shape[0] * shape[1] * 4
    if len(data) != expected_bytes:
        raise ValueError(f"{path}: expected {expected_bytes} bytes, found {len(data)}")
    values = list(struct.unpack("<%df" % (shape[0] * shape[1]), data))
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"{path}: non-finite FP32 operand")
    return values


def sample_indices(total: int, requested: int) -> List[int]:
    if total <= 0 or requested <= 0:
        raise ValueError("sample count and source size must be positive")
    count = min(total, requested)
    if count == 1:
        return [0]
    return [sample * (total - 1) // (count - 1) for sample in range(count)]


def f32_dot(lhs: Sequence[float], rhs: Sequence[float]) -> float:
    accumulator = f32(0.0)
    for left, right in zip(lhs, rhs):
        accumulator = f32(accumulator + f32(left * right))
    return accumulator


def load_modules(root: pathlib.Path) -> Tuple[str, List[Dict[str, object]]]:
    metadata = json.loads((root / "metadata.json").read_text(encoding="utf-8"))
    if metadata.get("format_version") not in (1, 2):
        raise ValueError("metadata.json: unsupported format_version")
    profile = metadata.get("profile", "legacy-qwen3-v1")
    if not isinstance(profile, str) or not profile:
        raise ValueError("metadata.json: profile must be a nonempty string")
    descriptors = metadata.get("modules")
    if not isinstance(descriptors, list) or not descriptors:
        raise ValueError("metadata.json: modules must be a nonempty array")

    modules: List[Dict[str, object]] = []
    for descriptor in descriptors:
        if not isinstance(descriptor, dict):
            raise ValueError("metadata.json: module descriptor must be an object")
        prefix = descriptor.get("artifact_prefix")
        if not isinstance(prefix, str) or not prefix or "/" in prefix or "\\" in prefix:
            raise ValueError("metadata.json: invalid module artifact_prefix")
        if descriptor.get("dtype") != "float32-le":
            raise ValueError(f"metadata.json: {prefix} must have dtype float32-le")
        input_shape = require_shape(descriptor.get("input_shape"), f"{prefix}.input")
        weight_shape = require_shape(descriptor.get("weight_shape"), f"{prefix}.weight")
        output_shape = require_shape(descriptor.get("output_shape"), f"{prefix}.output")
        if input_shape[1] != weight_shape[1] or output_shape != (input_shape[0], weight_shape[0]):
            raise ValueError(f"metadata.json: incompatible linear shapes for {prefix}")
        if input_shape[1] % DOT_WIDTH:
            raise ValueError(f"metadata.json: K for {prefix} must be divisible by four")
        modules.append(
            {
                "prefix": prefix,
                "input_shape": input_shape,
                "weight_shape": weight_shape,
                "input": read_tensor(root, prefix + ".input.f32", input_shape),
                "weight": read_tensor(root, prefix + ".weight.f32", weight_shape),
                "output": read_tensor(root, prefix + ".output.f32", output_shape),
            }
        )
    return profile, sorted(modules, key=lambda module: str(module["prefix"]))


def collect_mac(modules: Iterable[Dict[str, object]]) -> Tuple[List[float], List[float], List[float], int]:
    lhs: List[float] = []
    rhs: List[float] = []
    reference: List[float] = []
    terms_per_dot = 0
    for module in modules:
        tokens, input_k = module["input_shape"]  # type: ignore[misc]
        rows, weight_k = module["weight_shape"]  # type: ignore[misc]
        assert input_k == weight_k
        m, n, k = min(MAC_M, tokens), min(MAC_N, rows), min(MAC_K, input_k)
        if terms_per_dot == 0:
            terms_per_dot = k
        if terms_per_dot != k:
            raise ValueError("all MAC modules must have the same selected K")
        inputs: List[float] = module["input"]  # type: ignore[assignment]
        weights: List[float] = module["weight"]  # type: ignore[assignment]
        for token in range(m):
            for selected_row in range(n):
                row = 0 if n == 1 else selected_row * (rows - 1) // (n - 1)
                start_input = token * input_k
                start_weight = row * input_k
                left = inputs[start_input:start_input + k]
                right = weights[start_weight:start_weight + k]
                lhs.extend(left)
                rhs.extend(right)
                reference.append(f32_dot(left, right))
    return lhs, rhs, reference, terms_per_dot


def collect_dot_vectors(modules: Iterable[Dict[str, object]]) -> Tuple[List[float], List[float], List[float]]:
    lhs: List[float] = []
    rhs: List[float] = []
    reference: List[float] = []
    for module in modules:
        tokens, input_k = module["input_shape"]  # type: ignore[misc]
        rows, _ = module["weight_shape"]  # type: ignore[misc]
        groups = input_k // DOT_WIDTH
        source_vectors = tokens * rows * groups
        inputs: List[float] = module["input"]  # type: ignore[assignment]
        weights: List[float] = module["weight"]  # type: ignore[assignment]
        for flat in sample_indices(source_vectors, SAMPLE_COUNT):
            group = flat % groups
            row_and_token = flat // groups
            row = row_and_token % rows
            token = row_and_token // rows
            offset = group * DOT_WIDTH
            left = inputs[token * input_k + offset:token * input_k + offset + DOT_WIDTH]
            right = weights[row * input_k + offset:row * input_k + offset + DOT_WIDTH]
            lhs.extend(left)
            rhs.extend(right)
            reference.append(f32_dot(left, right))
    return lhs, rhs, reference


def collect_input_scalars(modules: Iterable[Dict[str, object]]) -> List[float]:
    values: List[float] = []
    for module in modules:
        inputs: List[float] = module["input"]  # type: ignore[assignment]
        values.extend(inputs[index] for index in sample_indices(len(inputs), SAMPLE_COUNT))
    return values


def load_add_samples(root: pathlib.Path) -> Tuple[List[float], List[float], List[float]]:
    metadata = json.loads((root / "metadata.json").read_text(encoding="utf-8"))
    operations = metadata.get("add_operations")
    if not isinstance(operations, list) or not operations:
        raise ValueError("metadata.json: add_operations must be a nonempty array")
    lhs: List[float] = []
    rhs: List[float] = []
    reference: List[float] = []
    for descriptor in sorted(operations, key=lambda operation: str(operation["artifact_prefix"])):
        if not isinstance(descriptor, dict) or descriptor.get("dtype") != "float32-le":
            raise ValueError("metadata.json: invalid add operation")
        prefix = descriptor.get("artifact_prefix")
        if not isinstance(prefix, str) or not prefix:
            raise ValueError("metadata.json: invalid add artifact_prefix")
        shape = require_shape(descriptor.get("shape"), f"{prefix}.add")
        left_values = read_tensor(root, prefix + ".lhs.f32", shape)
        right_values = read_tensor(root, prefix + ".rhs.f32", shape)
        output_values = read_tensor(root, prefix + ".output.f32", shape)
        for index in sample_indices(len(left_values), SAMPLE_COUNT):
            lhs.append(left_values[index])
            rhs.append(right_values[index])
            # ARA directly executes IEEE-754 FP32 add; the DFPVU output is Posit32.
            reference.append(f32(left_values[index] + right_values[index]))
    return lhs, rhs, reference


def c_array(name: str, values: Sequence[int], c_type: str = "uint32_t") -> str:
    body = ",\n    ".join("0x%08xu" % (value & 0xFFFFFFFF) for value in values)
    if not body:
        body = "0u"
    return "static const %s %s[%d] = {\n    %s\n};\n" % (c_type, name, max(1, len(values)), body)


def render_header(profile: str, modules: List[Dict[str, object]], add_root: pathlib.Path) -> str:
    mac_lhs, mac_rhs, mac_reference, mac_terms = collect_mac(modules)
    dot_lhs, dot_rhs, dot_reference = collect_dot_vectors(modules)
    scalar_values = collect_input_scalars(modules)
    add_lhs, add_rhs, add_reference = load_add_samples(add_root)
    mul_reference = [f32(left * right) for left, right in zip(dot_lhs, dot_rhs)]
    to_int_reference = [int(value) for value in scalar_values]
    if any(value < -2147483648 or value > 2147483647 for value in to_int_reference):
        raise ValueError("selected FP32-to-Int samples exceed Int32 range")

    lines = [
        "/* Generated by tools/generate_ara_workload_data.py; do not edit. */",
        "#ifndef ARA_DFPVU_WORKLOAD_GENERATED_DATA_H",
        "#define ARA_DFPVU_WORKLOAD_GENERATED_DATA_H",
        "#include <stdint.h>",
        '#define ARA_PROFILE "%s"' % profile.replace('"', ""),
        "#define ARA_CASE_MAC 1",
        "#define ARA_CASE_DOT 2",
        "#define ARA_CASE_MUL 3",
        "#define ARA_CASE_TO_INT 4",
        "#define ARA_CASE_ADD 5",
        "#define ARA_CASE_P16_PROXY 6",
        "#define ARA_MAC_TERM_COUNT %d" % mac_terms,
        "#define ARA_MAC_DOT_COUNT %d" % len(mac_reference),
        "#define ARA_DOT_COUNT %d" % len(dot_reference),
        "#define ARA_MUL_ELEMENT_COUNT %d" % len(mul_reference),
        "#define ARA_TO_INT_COUNT %d" % len(scalar_values),
        "#define ARA_ADD_COUNT %d" % len(add_reference),
        "",
        c_array("ara_mac_lhs_bits", [f32_bits(value) for value in mac_lhs]),
        c_array("ara_mac_rhs_bits", [f32_bits(value) for value in mac_rhs]),
        c_array("ara_mac_reference_bits", [f32_bits(value) for value in mac_reference]),
        c_array("ara_dot_lhs_bits", [f32_bits(value) for value in dot_lhs]),
        c_array("ara_dot_rhs_bits", [f32_bits(value) for value in dot_rhs]),
        c_array("ara_dot_reference_bits", [f32_bits(value) for value in dot_reference]),
        c_array("ara_mul_reference_bits", [f32_bits(value) for value in mul_reference]),
        c_array("ara_to_int_input_bits", [f32_bits(value) for value in scalar_values]),
        c_array("ara_to_int_reference_bits", [value & 0xFFFFFFFF for value in to_int_reference]),
        c_array("ara_add_lhs_bits", [f32_bits(value) for value in add_lhs]),
        c_array("ara_add_rhs_bits", [f32_bits(value) for value in add_rhs]),
        c_array("ara_add_reference_bits", [f32_bits(value) for value in add_reference]),
        "#endif  /* ARA_DFPVU_WORKLOAD_GENERATED_DATA_H */",
        "",
    ]
    return "\n".join(lines)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", required=True, type=pathlib.Path)
    parser.add_argument("--add-trace", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        profile, modules = load_modules(arguments.trace)
        header = render_header(profile, modules, arguments.add_trace)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(header, encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print("ARA workload data generation failed: %s" % error, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
