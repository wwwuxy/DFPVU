#!/usr/bin/env python3
import argparse
import json
import re
import sys
from pathlib import Path


REQUIRED_FIELDS = {
    "platform": ("run", "flow__platform", "run__flow__platform"),
    "final area": ("finish", "design__instance__area", "finish__design__instance__area"),
    "standard-cell count": ("finish", "design__instance__count__stdcell",
                            "finish__design__instance__count__stdcell"),
    "setup WNS": ("finish", "timing__setup__ws", "finish__timing__setup__ws"),
    "setup TNS": ("finish", "timing__setup__tns", "finish__timing__setup__tns"),
    "total power": ("finish", "power__total", "finish__power__total"),
    "internal power": ("finish", "power__internal__total", "finish__power__internal"),
    "switching power": ("finish", "power__switching__total", "finish__power__switching"),
    "leakage power": ("finish", "power__leakage__total", "finish__power__leakage"),
    "detailed-route DRC count": ("detailedroute", "route__drc_errors",
                                 "detailedroute__route__drc_errors"),
    "antenna-net count": ("detailedroute", "antenna__violating__nets",
                          "detailedroute__antenna__violating__nets"),
}


def _metric(metrics, path):
    stage, key, legacy_key = path
    stage_metrics = metrics.get(stage)
    if isinstance(stage_metrics, dict) and key in stage_metrics:
        return stage_metrics[key]
    return metrics.get(legacy_key)


def _required_values(metrics):
    return {name: _metric(metrics, path) for name, path in REQUIRED_FIELDS.items()}


def _missing(values):
    return [name for name, value in values.items()
            if value is None or value in ("ERR", "N/A")]


def _routing_overflow(path):
    text = Path(path).read_text(encoding="utf-8")
    overflow = None
    for report in text.split("Final congestion report:")[1:]:
        match = re.search(
            r"^Total\s+.*?\s+(\d+)\s*/\s*(\d+)\s*/\s*(\d+)\s*$",
            report,
            flags=re.MULTILINE,
        )
        if match:
            overflow = int(match.group(3))
    if overflow is None:
        raise ValueError("final routing overflow is unavailable")
    return overflow


def _markdown(summary):
    return """# OpenROAD PPA Summary

Status: {status}

## Run
- Platform: {platform}
- Target period (ns): {period}
- OpenROAD revision: {openroad_revision}
- ORFS revision: {orfs_revision}
- DFPVU revision: {revision}
- Power activity: {activity}

## Area
- Instance area (um^2): {area}
- Standard-cell count: {count}

## Timing
- Setup WNS (ns): {wns}
- Setup TNS (ns): {tns}

## Power
- Total (W): {total}
- Internal (W): {internal}
- Switching (W): {switching}
- Leakage (W): {leakage}

## Quality
- DRC errors: {drc}
- Antenna-violating nets: {antenna}
- Routing overflow: {overflow}

Power is estimated using ORFS/OpenSTA default activity; it is not workload-derived.
""".format(
        status=summary["status"], platform=summary["run"]["platform"],
        period=summary["run"]["target_period_ns"],
        openroad_revision=summary["run"]["openroad_revision"],
        orfs_revision=summary["run"]["orfs_revision"],
        revision=summary["run"]["dfpvu_revision"],
        activity=summary["run"]["power_activity"], area=summary["area"]["instance_area_um2"],
        count=summary["area"]["stdcell_count"], wns=summary["timing"]["setup_wns_ns"],
        tns=summary["timing"]["setup_tns_ns"], total=summary["power"]["total"],
        internal=summary["power"]["internal"], switching=summary["power"]["switching"],
        leakage=summary["power"]["leakage"], drc=summary["quality"]["drc_errors"],
        antenna=summary["quality"]["antenna_violating_nets"],
        overflow=summary["quality"]["routing_overflow"])


def main(argv=None):
    parser = argparse.ArgumentParser(description="Summarize ORFS PPA metrics")
    parser.add_argument("--metrics", required=True)
    parser.add_argument("--routing-log", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--output-md", required=True)
    parser.add_argument("--dfpvu-revision", required=True)
    parser.add_argument("--orfs-revision", required=True)
    parser.add_argument("--openroad-revision", required=True)
    parser.add_argument("--target-period-ns", required=True, type=float)
    args = parser.parse_args(argv)
    try:
        metrics = json.loads(Path(args.metrics).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print("Unable to read metrics: {}".format(exc), file=sys.stderr)
        return 2
    try:
        routing_overflow = _routing_overflow(args.routing_log)
    except (OSError, ValueError) as exc:
        print("Unable to read routing overflow: {}".format(exc), file=sys.stderr)
        return 2
    values = _required_values(metrics)
    missing = _missing(values)
    if missing:
        print("Missing mandatory metrics: {}".format(", ".join(missing)), file=sys.stderr)
        return 2
    quality_failures = []
    if values["detailed-route DRC count"] != 0:
        quality_failures.append("DRC errors={}".format(values["detailed-route DRC count"]))
    if values["antenna-net count"] != 0:
        quality_failures.append("antenna-violating nets={}".format(values["antenna-net count"]))
    if routing_overflow != 0:
        quality_failures.append("routing overflow={}".format(routing_overflow))
    if quality_failures:
        print("Physical implementation is unsuccessful: {}".format(", ".join(quality_failures)),
              file=sys.stderr)
        return 2
    revisions = {
        "DFPVU": args.dfpvu_revision,
        "ORFS": args.orfs_revision,
        "OpenROAD": args.openroad_revision,
    }
    invalid_revisions = [name for name, revision in revisions.items()
                         if not revision or revision in ("N/A", "unknown")]
    if invalid_revisions:
        print("Missing actual revisions: {}".format(", ".join(invalid_revisions)), file=sys.stderr)
        return 2
    if args.target_period_ns <= 0:
        print("Target period must be positive", file=sys.stderr)
        return 2
    summary = {
        "status": "complete",
        "run": {"platform": values["platform"],
                "target_period_ns": args.target_period_ns,
                "openroad_revision": args.openroad_revision,
                "orfs_revision": args.orfs_revision,
                "dfpvu_revision": args.dfpvu_revision,
                "power_activity": "ORFS/OpenSTA default activity; not workload-derived"},
        "area": {"instance_area_um2": values["final area"],
                 "stdcell_count": values["standard-cell count"]},
        "timing": {"setup_wns_ns": values["setup WNS"],
                   "setup_tns_ns": values["setup TNS"]},
        "power": {"units": "W",
                  "total": values["total power"],
                  "internal": values["internal power"],
                  "switching": values["switching power"],
                  "leakage": values["leakage power"]},
        "quality": {"drc_errors": values["detailed-route DRC count"],
                    "antenna_violating_nets": values["antenna-net count"],
                    "routing_overflow": routing_overflow},
    }
    Path(args.output_json).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    Path(args.output_md).write_text(_markdown(summary), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
