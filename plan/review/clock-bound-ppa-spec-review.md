# Clock-Bound PPA Spec Review

- Scope is `PvuTop`, Nangate45, 45.00 ns target period, and 55% target core utilization.
- The checked SDC binds `dfpvu_vclk` to `[get_ports clock]`; the CTS log identifies one real clock net with 8,857 sinks.
- The required cost categories are present: mapped cell count/area, core and die dimensions, utilization, CTS, post-GRT timing and routing, detailed-route DRC/interconnect, and vectorless power label.
- The report separates reproduced synthesis-to-GRT values from the retained completed detailed-route run under the same configuration.
- Ara physical comparisons are rejected because the local Ara configuration has a different top-level functional scope.
- Outcome: acceptable as a DFPVU-only implementation-cost dataset with explicit near-closure and non-signoff limitations.
