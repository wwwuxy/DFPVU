#!/usr/bin/env python3
import argparse
import json
import sys
from pathlib import Path


REQUIRED_FIELDS = {
    "platform": "run__flow__platform",
    "final area": "finish__design__instance__area",
    "standard-cell count": "finish__design__instance__count__stdcell",
    "setup WNS": "finish__timing__setup__ws",
    "setup TNS": "finish__timing__setup__tns",
    "total power": "finish__power__total",
    "internal power": "finish__power__internal",
    "switching power": "finish__power__switching",
    "leakage power": "finish__power__leakage",
    "detailed-route DRC count": "detailedroute__route__drc_errors",
    "antenna-net count": "detailedroute__antenna__violating__nets",
}


def _missing(metrics):
    return [name for name, key in REQUIRED_FIELDS.items()
            if key not in metrics or metrics[key] in ("ERR", "N/A")]


def _markdown(summary):
    return """# OpenROAD PPA Summary

Status: {status}

## Run
- Platform: {platform}
- OpenROAD commit: {commit}
- DFPVU revision: {revision}
- Power activity: {activity}

## Area
- Instance area (um^2): {area}
- Standard-cell count: {count}

## Timing
- Setup WNS (ns): {wns}
- Setup TNS (ns): {tns}

## Power
- Total: {total}
- Internal: {internal}
- Switching: {switching}
- Leakage: {leakage}

## Quality
- DRC errors: {drc}
- Antenna-violating nets: {antenna}

Power is estimated using ORFS/OpenSTA default activity; it is not workload-derived.
""".format(
        status=summary["status"], platform=summary["run"]["platform"],
        commit=summary["run"]["openroad_commit"], revision=summary["run"]["dfpvu_revision"],
        activity=summary["run"]["power_activity"], area=summary["area"]["instance_area_um2"],
        count=summary["area"]["stdcell_count"], wns=summary["timing"]["setup_wns_ns"],
        tns=summary["timing"]["setup_tns_ns"], total=summary["power"]["total"],
        internal=summary["power"]["internal"], switching=summary["power"]["switching"],
        leakage=summary["power"]["leakage"], drc=summary["quality"]["drc_errors"],
        antenna=summary["quality"]["antenna_violating_nets"])


def main(argv=None):
    parser = argparse.ArgumentParser(description="Summarize ORFS PPA metrics")
    parser.add_argument("--metrics", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--output-md", required=True)
    parser.add_argument("--dfpvu-revision", required=True)
    args = parser.parse_args(argv)
    try:
        metrics = json.loads(Path(args.metrics).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print("Unable to read metrics: {}".format(exc), file=sys.stderr)
        return 2
    missing = _missing(metrics)
    if missing:
        print("Missing mandatory metrics: {}".format(", ".join(missing)), file=sys.stderr)
        return 2
    summary = {
        "status": "complete",
        "run": {"platform": metrics["run__flow__platform"],
                "openroad_commit": metrics.get("run__flow__openroad_commit", "unknown"),
                "dfpvu_revision": args.dfpvu_revision,
                "power_activity": "ORFS/OpenSTA default activity; not workload-derived"},
        "area": {"instance_area_um2": metrics["finish__design__instance__area"],
                 "stdcell_count": metrics["finish__design__instance__count__stdcell"]},
        "timing": {"setup_wns_ns": metrics["finish__timing__setup__ws"],
                   "setup_tns_ns": metrics["finish__timing__setup__tns"]},
        "power": {"total": metrics["finish__power__total"],
                  "internal": metrics["finish__power__internal"],
                  "switching": metrics["finish__power__switching"],
                  "leakage": metrics["finish__power__leakage"]},
        "quality": {"drc_errors": metrics["detailedroute__route__drc_errors"],
                    "antenna_violating_nets": metrics["detailedroute__antenna__violating__nets"]},
    }
    Path(args.output_json).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    Path(args.output_md).write_text(_markdown(summary), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
