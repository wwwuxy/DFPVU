# Task Packet: Clock-Bound OpenROAD PPA Baseline

- Scope: produce a reproducible DFPVU physical-implementation dataset suitable for the IPDPS evaluation section.
- Files to read: `openroad/nangate45/config.mk`, `openroad/nangate45/constraint.sdc`, `openroad/run_nangate45_ppa.sh`, generated reports, and the existing PPA summarizer.
- Files allowed to edit: the SDC regression test, clock constraint, PPA output directory, and experiment-planning records.
- Required skills: systematic debugging, test-driven development, experiment-results planning, verification.
- Evidence/data inputs: OpenROAD-flow-scripts Nangate45 reports generated after binding the clock port; no synthetic PPA values.
- Required artifacts: raw metrics, a concise PPA summary, a paper-table data row, and explicit validity/limitation labels. The delivered data summary is `docs/result/dfpvu_openroad_ppa_45ns.md`.
- Rejection checks: no virtual clock; CTS must identify at least one clock net; register launch/capture paths must be present; do not compare against Ara PPA without matching synthesizable RTL and identical flow conditions.
- Validation commands: constraint regression, configuration tests, metrics extraction, report inspection, and consistency checks against the generated SDC.
