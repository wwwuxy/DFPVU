#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
make -C "$repo_root" -n menuconfig | rg "^menuconfig$"
actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=y" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm;echo injected" "CONFIG_MATRIX_GEMM_P32_M=2" "CONFIG_MATRIX_GEMM_P32_N=3" "CONFIG_MATRIX_GEMM_P32_K=5" | rg "^./obj_dir/VPvuTop")
expected="./obj_dir/VPvuTop  \"/tmp/llm;echo injected\" \"2\" \"3\" \"5\""

if [[ "$actual" != "$expected" ]]; then
  printf "expected shell-quoted Kconfig trace arguments:\n  %s\nactual:\n  %s\n" "$expected" "$actual" >&2
  exit 1
fi

scratch_dir=$(mktemp -d)
trap 'rm -rf "$scratch_dir"' EXIT
missing_trace="$scratch_dir/trace"
missing_model="$scratch_dir/model-is-not-installed"
if output=$(make -C "$repo_root" --no-print-directory llm_p32_mac_trace_config "CONFIG_LLM_P32_MAC_WORKLOAD=y" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_AUTO_EXPORT_TRACE=y" "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=y" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=n" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n" "CONFIG_LLM_P32_MAC_TRACE_DIR=$missing_trace" "CONFIG_LLM_P32_MODEL_DIR=$missing_model" 2>&1); then
  printf "expected a missing local model directory to fail, but command succeeded\n" >&2
  exit 1
fi
if ! printf '%s\n' "$output" | rg -F "Local LLM model directory is missing: $missing_model" >/dev/null; then
  printf "expected a clear missing-model diagnostic, got:\n%s\n" "$output" >&2
  exit 1
fi

printf "LLM Kconfig trace arguments are shell-quoted\n"

conversion_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=y" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-conversion" "CONFIG_LLM_P32_CONVERSION_TENSOR=weight" "CONFIG_LLM_P32_CONVERSION_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
conversion_expected="./obj_dir/VPvuTop  \"/tmp/llm-conversion\" \"weight\" \"256\""

if [[ "$conversion_actual" != "$conversion_expected" ]]; then
  printf "expected conversion Kconfig arguments:\n  %s\nactual:\n  %s\n" "$conversion_expected" "$conversion_actual" >&2
  exit 1
fi

printf "LLM conversion Kconfig arguments select source and sample count\n"

p16_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=y" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-p16" "CONFIG_LLM_P32_CONVERSION_TENSOR=weight" "CONFIG_LLM_P32_CONVERSION_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
p16_expected="./obj_dir/VPvuTop  \"/tmp/llm-p16\" \"weight\" \"256\""
if [[ "$p16_actual" != "$p16_expected" ]]; then
  printf "expected P32-to-P16 Kconfig arguments:\n  %s\nactual:\n  %s\n" "$p16_expected" "$p16_actual" >&2
  exit 1
fi
printf "LLM P32-to-P16 Kconfig arguments select source and sample count\n"

python3 -c 'import kconfiglib,sys; kconf=kconfiglib.Kconfig(sys.argv[1], warn=False); kconf.load_config(sys.argv[2]); kconf.syms["LLM_P32_TO_P16_WORKLOAD"].set_value("y"); assert kconf.syms["LLM_P32_CONVERSION_TENSOR_WEIGHT"].visibility; assert kconf.syms["LLM_P32_CONVERSION_SAMPLES_256"].visibility; assert kconf.syms["LLM_P32_CONVERSION_TENSOR"].str_value in ("input", "weight", "output"); assert int(kconf.syms["LLM_P32_CONVERSION_SAMPLE_COUNT"].str_value) > 0' "$repo_root/Kconfig" "$repo_root/.config"
printf "LLM P32-to-P16 exposes shared tensor and sample settings\n"

dot_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=y" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-dot" "CONFIG_LLM_P32_DOT_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
dot_expected="./obj_dir/VPvuTop  \"/tmp/llm-dot\" \"256\""
if [[ "$dot_actual" != "$dot_expected" ]]; then
  printf "expected Dot Kconfig arguments:\n  %s\nactual:\n  %s\n" "$dot_expected" "$dot_actual" >&2
  exit 1
fi
printf "LLM Dot Kconfig arguments select sample count\n"

mul_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=y" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-mul" "CONFIG_LLM_P32_MUL_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
mul_expected="./obj_dir/VPvuTop  \"/tmp/llm-mul\" \"256\""
if [[ "$mul_actual" != "$mul_expected" ]]; then
  printf "expected Multiply Kconfig arguments:\n  %s\nactual:\n  %s\n" "$mul_expected" "$mul_actual" >&2
  exit 1
fi
printf "LLM Multiply Kconfig arguments select sample count\n"

to_int_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=y" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_MAC_TRACE_DIR=/tmp/llm-to-int" "CONFIG_LLM_P32_TO_INT_TENSOR=output" "CONFIG_LLM_P32_TO_INT_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
to_int_expected="./obj_dir/VPvuTop  \"/tmp/llm-to-int\" \"output\" \"256\""
if [[ "$to_int_actual" != "$to_int_expected" ]]; then
  printf "expected P32-to-Int Kconfig arguments:\n  %s\nactual:\n  %s\n" "$to_int_expected" "$to_int_actual" >&2
  exit 1
fi
printf "LLM P32-to-Int Kconfig arguments select source and sample count\n"
add_actual=$(make -C "$repo_root" -n run "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=y" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_ADD_TRACE_DIR=/tmp/llm-add" "CONFIG_LLM_P32_ADD_SAMPLE_COUNT=256" | rg "^./obj_dir/VPvuTop")
add_expected="./obj_dir/VPvuTop  \"/tmp/llm-add\" \"256\""
if [[ "$add_actual" != "$add_expected" ]]; then
  printf "expected Add Kconfig arguments:\n  %s\nactual:\n  %s\n" \
    "$add_expected" "$add_actual" >&2
  exit 1
fi
add_export=$(make -C "$repo_root" -n llm_p32_add_trace_config "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=y" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_ADD_TRACE_DIR=/tmp/llm-add" 2>&1 || true)
if ! printf '%s\n' "$add_export" | rg -F -- '--capture-add' >/dev/null; then
  printf "expected Add trace export command to enable --capture-add, got:\n%s\n" \
    "$add_export" >&2
  exit 1
fi
printf "LLM P32 Add Kconfig arguments select trace, sample count, and Add export\n"

assert_profile_default_add_route() {
  local profile=$1 expected_model=$2 expected_trace=$3
  shift 3
  local route
  route=$(make -C "$repo_root" -n run \
    "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=y" "CONFIG_LLM_P32_PATH_MODE_PROFILE_DEFAULTS=y" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=n" \
    "CONFIG_LLM_P32_MODEL_DIR=$scratch_dir/stale-model" \
    "CONFIG_LLM_P32_MAC_TRACE_DIR=$scratch_dir/stale-trace" \
    "CONFIG_LLM_P32_ADD_TRACE_DIR=$scratch_dir/stale-add-trace" \
    "CONFIG_LLM_P32_ADD_SAMPLE_COUNT=256" "$@")
  if ! printf '%s\n' "$route" | rg -F -- "--profile \"$profile\" --model \"$expected_model\" --output \"$expected_trace\"" >/dev/null; then
    printf "expected %s profile defaults to ignore stale paths, got:\n%s\n" "$profile" "$route" >&2
    exit 1
  fi
  local runner expected_runner
  runner=$(printf '%s\n' "$route" | rg '^./obj_dir/VPvuTop')
  expected_runner="./obj_dir/VPvuTop  \"$expected_trace\" \"256\""
  if [[ "$runner" != "$expected_runner" ]]; then
    printf "expected %s Add runner path:\n  %s\nactual:\n  %s\n" "$profile" "$expected_runner" "$runner" >&2
    exit 1
  fi
}

assert_profile_default_add_route qwen3-0.6b /root/models/Qwen3-0.6B /tmp/qwen3-0.6b-p32-add-trace "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=y" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=n" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n"
assert_profile_default_add_route gemma3-1b /root/models/gemma-3-1b-it /tmp/gemma-3-1b-p32-add-trace "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=n" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=y" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n"
assert_profile_default_add_route phi4-mini /root/models/phi-4-mini-instruct /tmp/phi-4-mini-p32-add-trace "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=n" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=n" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=y"
printf "LLM profile-default Add routes ignore stale configured paths\n"

assert_profile_default_linear_route() {
  local profile=$1 expected_model=$2 expected_trace=$3
  shift 3
  local route
  route=$(make -C "$repo_root" -n run \
    "CONFIG_LLM_P32_MAC_WORKLOAD=y" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_PROFILE_DEFAULTS=y" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=n" \
    "CONFIG_LLM_P32_MODEL_DIR=$scratch_dir/stale-model" \
    "CONFIG_LLM_P32_MAC_TRACE_DIR=$scratch_dir/stale-trace" \
    "CONFIG_LLM_P32_ADD_TRACE_DIR=$scratch_dir/stale-add-trace" \
    "CONFIG_MATRIX_GEMM_P32_M=2" "CONFIG_MATRIX_GEMM_P32_N=3" "CONFIG_MATRIX_GEMM_P32_K=5" "$@")
  if ! printf '%s\n' "$route" | rg -F -- "--profile \"$profile\" --model \"$expected_model\" --output \"$expected_trace\"" >/dev/null; then
    printf "expected %s profile defaults to ignore stale linear paths:\n%s\n" "$profile" "$route" >&2
    exit 1
  fi
  printf '%s\n' "$route" | rg -F -- '"linear_module_scope"' >/dev/null
  if printf '%s\n' "$route" | rg -F -- '--module ' >/dev/null; then
    printf 'profile-default linear export must not select one module\n' >&2
    exit 1
  fi
  local runner expected_runner
  runner=$(printf '%s\n' "$route" | rg '^./obj_dir/VPvuTop')
  expected_runner="./obj_dir/VPvuTop  \"$expected_trace\" \"2\" \"3\" \"5\""
  if [[ "$runner" != "$expected_runner" ]]; then
    printf "expected %s MAC runner path:\n  %s\nactual:\n  %s\n" "$profile" "$expected_runner" "$runner" >&2
    exit 1
  fi
}

assert_profile_default_linear_route qwen3-0.6b /root/models/Qwen3-0.6B /tmp/qwen3-0.6b-p32-linear-trace "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=y" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=n" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n"
assert_profile_default_linear_route gemma3-1b /root/models/gemma-3-1b-it /tmp/gemma-3-1b-p32-linear-trace "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=n" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=y" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n"
assert_profile_default_linear_route phi4-mini /root/models/phi-4-mini-instruct /tmp/phi-4-mini-p32-linear-trace "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=n" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=n" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=y"
printf "LLM profile-default linear routes ignore stale configured paths\n"

python3 -c 'import kconfiglib,sys; kconf=kconfiglib.Kconfig(sys.argv[1], warn=False); kconf.load_config(sys.argv[2]); kconf.syms["LLM_P32_ADD_WORKLOAD"].set_value("y"); defaults=("LLM_P32_PATH_MODE_PROFILE_DEFAULTS", "LLM_P32_ADD_SAMPLES_256"); assert all(kconf.syms[name].visibility for name in defaults), "default Add path mode is unavailable"; assert kconf.syms["LLM_P32_ADD_SAMPLE_COUNT"].str_value == "256", "Add sample default is unavailable"; kconf.syms["LLM_P32_PATH_MODE_CUSTOM"].set_value("y"); custom=("LLM_P32_MODEL_DIR", "LLM_P32_ADD_TRACE_DIR"); assert all(kconf.syms[name].visibility for name in custom), "custom Add paths are unavailable"' "$repo_root/Kconfig" "$repo_root/.config"
printf "LLM P32 Add Kconfig selection exposes its trace and sample settings\n"

stale_trace="$scratch_dir/stale-profile-trace"
mkdir -p "$stale_trace"
printf '{"format_version":2,"profile":"qwen3-0.6b"}\n' >"$stale_trace/metadata.json"
if output=$(make -C "$repo_root" --no-print-directory llm_p32_trace_config "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=y" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=n" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=y" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_MAC_TRACE_DIR=$stale_trace" 2>&1); then
  printf "expected a stale LLM trace profile to fail, but command succeeded\n" >&2
  exit 1
fi
if ! printf '%s\n' "$output" | rg -F 'LLM trace profile does not match selected Kconfig profile' >/dev/null; then
  printf "expected a clear stale-profile diagnostic, got:\n%s\n" "$output" >&2
  exit 1
fi

printf "LLM trace profile is validated against the selected Kconfig model\n"

profile_default_subset_trace="$scratch_dir/profile-default-explicit-subset-trace"
mkdir -p "$profile_default_subset_trace"
printf '{"format_version":2,"profile":"qwen3-0.6b","linear_module_scope":"explicit-subset"}\n' >"$profile_default_subset_trace/metadata.json"
if output=$(make -C "$repo_root" --no-print-directory llm_p32_trace_config "CONFIG_LLM_P32_MAC_WORKLOAD=y" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_PROFILE_DEFAULTS=y" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=n" "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=y" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=n" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n" "LLM_P32_DEFAULT_TRACE_DIR=$profile_default_subset_trace" 2>&1); then
  printf "expected profile-default explicit-subset trace to fail, but command succeeded\n" >&2
  exit 1
fi
if ! printf '%s\n' "$output" | rg -F "LLM default linear trace is not an all-decoder-linear export: $profile_default_subset_trace" >/dev/null; then
  printf "expected a clear profile-default scope diagnostic, got:\n%s\n" "$output" >&2
  exit 1
fi
printf "LLM profile-default paths reject explicit-subset traces\n"

custom_subset_trace="$scratch_dir/custom-explicit-subset-trace"
mkdir -p "$custom_subset_trace"
printf '{"format_version":2,"profile":"qwen3-0.6b","linear_module_scope":"explicit-subset"}\n' >"$custom_subset_trace/metadata.json"
if ! output=$(make -C "$repo_root" --no-print-directory llm_p32_trace_config "CONFIG_LLM_P32_MAC_WORKLOAD=y" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=n" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=y" "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=y" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=n" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n" "CONFIG_LLM_P32_MAC_TRACE_DIR=$custom_subset_trace" 2>&1); then
  printf "expected Custom explicit-subset trace to be reused, got:\n%s\n" "$output" >&2
  exit 1
fi
if ! printf '%s\n' "$output" | rg -F "LLM trace: reusing $custom_subset_trace" >/dev/null; then
  printf "expected Custom explicit-subset reuse diagnostic, got:\n%s\n" "$output" >&2
  exit 1
fi
printf "LLM Custom paths reuse explicit-subset traces\n"

# A default Add trace is an Add artifact, not an all-linear artifact.  The
# matching default linear trace must keep rejecting a one-module export.
assert_profile_default_trace_contract() {
  local workload=$1 profile=$2 selector=$3 trace_kind=$4
  local trace="$scratch_dir/${profile}-${workload#CONFIG_LLM_P32_}-${trace_kind}"
  mkdir -p "$trace"
  local expected_message target
  if [[ "$trace_kind" == linear ]]; then
    printf '{"format_version":2,"profile":"%s","linear_module_scope":"explicit-subset"}\n' "$profile" >"$trace/metadata.json"
    expected_message="LLM default linear trace is not an all-decoder-linear export: $trace"
    target=llm_p32_trace_config
  else
    printf '{"format_version":2,"profile":"%s","add_operations":[]}\n' "$profile" >"$trace/metadata.json"
    expected_message="LLM trace: reusing $trace"
    target=llm_p32_add_trace_config
  fi

  local -a workload_config=(
    "CONFIG_LLM_P32_MAC_WORKLOAD=n"
    "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n"
    "CONFIG_LLM_P32_TO_P16_WORKLOAD=n"
    "CONFIG_LLM_P32_DOT_WORKLOAD=n"
    "CONFIG_LLM_P32_MUL_WORKLOAD=n"
    "CONFIG_LLM_P32_TO_INT_WORKLOAD=n"
    "CONFIG_LLM_P32_ADD_WORKLOAD=n"
    "${workload}=y"
    "CONFIG_LLM_P32_PATH_MODE_PROFILE_DEFAULTS=y"
    "CONFIG_LLM_P32_PATH_MODE_CUSTOM=n"
    "CONFIG_LLM_P32_AUTO_EXPORT_TRACE=n"
  )
  local -a profile_config=(
    "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=n"
    "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=n"
    "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n"
    "${selector}=y"
  )
  local trace_override
  if [[ "$trace_kind" == linear ]]; then
    trace_override="LLM_P32_DEFAULT_TRACE_DIR=$trace"
  else
    trace_override="LLM_P32_DEFAULT_ADD_TRACE_DIR=$trace"
  fi

  local output
  if [[ "$trace_kind" == linear ]]; then
    if output=$(make -C "$repo_root" --no-print-directory "$target" "${workload_config[@]}" "${profile_config[@]}" "$trace_override" 2>&1); then
      printf "expected %s %s default linear trace to reject explicit-subset metadata\n" "$profile" "$workload" >&2
      exit 1
    fi
  elif ! output=$(make -C "$repo_root" --no-print-directory "$target" "${workload_config[@]}" "${profile_config[@]}" "$trace_override" 2>&1); then
    printf "expected %s default Add trace without linear scope to be accepted, got:\n%s\n" "$profile" "$output" >&2
    exit 1
  fi
  if ! printf '%s\n' "$output" | rg -F "$expected_message" >/dev/null; then
    printf "expected %s %s scope-contract diagnostic, got:\n%s\n" "$profile" "$workload" "$output" >&2
    exit 1
  fi
}

for profile_case in \
  'qwen3-0.6b|CONFIG_LLM_P32_PROFILE_QWEN3_0_6B' \
  'gemma3-1b|CONFIG_LLM_P32_PROFILE_GEMMA3_1B' \
  'phi4-mini|CONFIG_LLM_P32_PROFILE_PHI4_MINI'; do
  IFS='|' read -r profile selector <<<"$profile_case"
  for workload in \
    CONFIG_LLM_P32_MAC_WORKLOAD \
    CONFIG_LLM_P32_CONVERSION_WORKLOAD \
    CONFIG_LLM_P32_TO_P16_WORKLOAD \
    CONFIG_LLM_P32_DOT_WORKLOAD \
    CONFIG_LLM_P32_MUL_WORKLOAD \
    CONFIG_LLM_P32_TO_INT_WORKLOAD; do
    assert_profile_default_trace_contract "$workload" "$profile" "$selector" linear
  done
  assert_profile_default_trace_contract CONFIG_LLM_P32_ADD_WORKLOAD "$profile" "$selector" add
done
printf "LLM default trace scope contracts cover every workload and profile\n"

profileless_default_add_trace="$scratch_dir/profileless-default-add-trace"
mkdir -p "$profileless_default_add_trace"
printf '{"format_version":2,"add_operations":[]}\n' >"$profileless_default_add_trace/metadata.json"
if output=$(make -C "$repo_root" --no-print-directory llm_p32_add_trace_config "CONFIG_LLM_P32_MAC_WORKLOAD=n" "CONFIG_LLM_P32_CONVERSION_WORKLOAD=n" "CONFIG_LLM_P32_TO_P16_WORKLOAD=n" "CONFIG_LLM_P32_DOT_WORKLOAD=n" "CONFIG_LLM_P32_MUL_WORKLOAD=n" "CONFIG_LLM_P32_TO_INT_WORKLOAD=n" "CONFIG_LLM_P32_ADD_WORKLOAD=y" "CONFIG_LLM_P32_PATH_MODE_PROFILE_DEFAULTS=y" "CONFIG_LLM_P32_PATH_MODE_CUSTOM=n" "CONFIG_LLM_P32_PROFILE_QWEN3_0_6B=y" "CONFIG_LLM_P32_PROFILE_GEMMA3_1B=n" "CONFIG_LLM_P32_PROFILE_PHI4_MINI=n" "LLM_P32_DEFAULT_ADD_TRACE_DIR=$profileless_default_add_trace" 2>&1); then
  printf "expected a profileless default Add trace to fail, but command succeeded\n" >&2
  exit 1
fi
if ! printf '%s\n' "$output" | rg -F "LLM default trace lacks required profile: $profileless_default_add_trace" >/dev/null; then
  printf "expected a clear profileless-default-Add diagnostic, got:\n%s\n" "$output" >&2
  exit 1
fi
printf "LLM profile-default Add traces require a matching profile\n"
