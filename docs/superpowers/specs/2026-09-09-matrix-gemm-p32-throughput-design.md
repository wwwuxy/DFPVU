# P32 GEMM Throughput Benchmark Design

## Goal

Measure the cycle-level throughput of the existing four-lane `Posit<32,2>`
MAC datapath for controlled GEMM shapes `A[M,K] x B[K,N] = C[M,N]`. The
benchmark must expose how `M`, `N`, and `K` affect end-to-end cycles and
effective valid MACs per cycle without presenting Verilator wall-clock time as
hardware frequency or TOPS.

## Scope

- Reuse the existing `PvuTop` `op=11` raw P32 MAC datapath; do not modify RTL.
- Add one Kconfig-selected Verilator C++ main for a deterministic single GEMM
  case selected by `--m`, `--n`, `--k`, and `--seed` command-line options.
- Use direct raw P32 bit patterns from a fixed finite corpus for matrix values.
- Compare every output word against the same-order SoftPosit `p32_mulAdd`
  recurrence and fail on any mismatch.
- Emit one machine-readable CSV record plus a readable summary for each run.

## Non-goals

- This is not a matrix-storage, DMA, cache, tiling, or full-system benchmark.
- It does not change the static vector width, so it does not compare four-,
  eight-, or sixteen-lane RTL implementations.
- It does not report post-synthesis timing, clock frequency, TOPS, or host
  simulation wall-clock time.

## Mapping and Scheduling

For each output coordinate `(m,n)`, the driver processes consecutive K groups
of at most four terms. It issues one raw P32 `op=11` request per group,
initializing `posit_i3` to zero and feeding the response word into the next
group for that coordinate. The final partial group sets `vector_size` to one,
two, or three; the inactive lane input words are zero.

Each output chain is dependent on its previous response. The driver keeps
multiple independent `(m,n)` chains in flight and selects a ready chain in
round-robin order. Tags identify responses and prevent a response from being
applied to the wrong accumulator. The DUT runs with `out_ready=1`.

## Metrics

The measurement interval begins at the first accepted request and ends at the
final accepted response. For one case:

- `requests = M * N * ceil(K / 4)`
- `valid_mac_terms = M * N * K`
- `mac_per_cycle = valid_mac_terms / cycles`
- `request_per_cycle = requests / cycles`
- `lane_utilization = valid_mac_terms / (4 * requests)`

The report also includes exact mismatch count and the maximum dependency-chain
latency observed from request acceptance to response acceptance. A nonzero
SoftPosit mismatch causes a failing process exit and invalidates throughput
interpretation.

## Experiment Matrix

The driver executes a requested single case. The shell test exercises `(2,3,5)`
to cover rectangular dimensions and a tail request. Comparative runs use:

- M sweep: `(1/2/4/8/16/32/64/128,64,64)`.
- N sweep: `(64,1/2/4/8/16/32/64/128,64)`.
- K sweep: `(64,64,1/2/3/4/5/8/16/32/64/128/256/512)`.
- Balanced sweep: `(4,4,4)` through `(128,128,128)`.

All runs use the same seed unless the data-distribution effect itself is being
studied. The final CSV records the raw dimensions and seed so rows are
reproducible.

## Acceptance Criteria

1. `make run MATRIX_GEMM_P32_ARGS="--m 2 --n 3 --k 5 --seed 7"` selects one
   matrix benchmark main and exits successfully.
2. Its CSV reports `requests=12`, `valid_mac_terms=30`, and
   `lane_utilization=0.625000`.
3. The run reports `exact_mismatches=0`; any discrepancy prints matrix
   coordinates and expected/actual raw P32 words and exits nonzero.
4. Invalid or zero dimensions and unknown options exit nonzero with a concise
   diagnostic.
5. Existing Qwen3 and MAC regression source behavior remains unchanged.
