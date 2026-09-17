# Clock-Bound PPA Quality Review

- Every numerical PPA value in the English and Chinese summaries has an identified raw OpenROAD log or report source.
- The summaries do not call the layout timing-clean, DRC-clean, manufacturable, signoff-ready, or energy-measured.
- The 19.8 W power figure is labeled as OpenSTA vectorless/default-activity output and is excluded from workload-energy claims.
- The DRC figure is tied to `DETAILED_ROUTE_END_ITERATION=1`, including the iteration-0 to iteration-1 reduction.
- The Ara section states `N/A` for same-scope PPA and prohibits area/power/energy ratio claims.
- Outcome: wording is suitable for a conference evaluation table when the listed limitations accompany it.
