# Method–Experiment Traceability

| Contribution | Method element | Evidence | Allowed claim | Status |
|---|---|---|---|---|
| High-throughput vector arithmetic | Four-lane DFPVU request interface and pipelined datapath | Direct and matrix RTL cycle tables | Higher useful work per cycle than four-lane Ara on measured microbenchmarks | Supported with semantic qualification |
| Low scheduling overhead | Near-one accepted request per cycle and six-cycle fixed matrix overhead | DFPVU matrix preset results | DFPVU approaches four useful MACs/cycle for large, fully utilized shapes | Supported |
| Posit/IEEE dual-format support | Native conversion and Posit arithmetic operations under RVV | Conversion and arithmetic workload results | DFPVU exposes functionality absent from unmodified Ara's IEEE FP vector path | Supported as a capability claim |
| Area and energy efficiency | Clock-bound OpenROAD PPA baseline | DFPVU Nangate45 implementation reports; Ara RTL/configuration required for a matched row | Report DFPVU implementation cost; make no Ara superiority claim unless the flow is matched | In progress |
| End-to-end LLM benefit | Full-model inference evaluation | Not yet available | No token/s or model-accuracy claim | Deferred |
