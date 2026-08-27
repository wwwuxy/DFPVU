import json
import tempfile
import unittest
from pathlib import Path

from openroad.summarize_ppa import main


class SummarizePpaTest(unittest.TestCase):
    @staticmethod
    def _write_routing_log(root, overflow=0):
        routing_log = root / "5_1_grt.log"
        routing_log.write_text(
            "[INFO GRT-0096] Final congestion report:\n"
            "Layer Resource Demand Usage Max H / Max V / Total Congestion\n"
            "Total 100 50 50.00% 0 / 0 / {}\n".format(overflow),
            encoding="utf-8",
        )
        return routing_log

    def test_extracts_current_nested_orfs_metrics(self):
        metrics = {
            "run": {
                "flow__platform": "nangate45",
                "flow__openroad_commit": "abc123",
            },
            "finish": {
                "design__instance__area": 1234.5,
                "design__instance__count__stdcell": 321,
                "timing__setup__ws": -1.25,
                "timing__setup__tns": -42.0,
                "power__total": 0.0042,
                "power__internal__total": 0.0020,
                "power__switching__total": 0.0015,
                "power__leakage__total": 0.0007,
            },
            "detailedroute": {
                "route__drc_errors": 0,
                "antenna__violating__nets": 0,
            },
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            metrics_path = root / "metrics.json"
            output_json = root / "summary.json"
            output_md = root / "summary.md"
            metrics_path.write_text(json.dumps(metrics), encoding="utf-8")
            routing_log = self._write_routing_log(root)

            result = main([
                "--metrics", str(metrics_path),
                "--routing-log", str(routing_log),
                "--output-json", str(output_json),
                "--output-md", str(output_md),
                "--dfpvu-revision", "deadbeef",
                "--openroad-revision", "feedface",
                "--orfs-revision", "cafebabe",
                "--target-period-ns", "10.0",
            ])

            self.assertEqual(result, 0)
            summary = json.loads(output_json.read_text(encoding="utf-8"))
            self.assertEqual(summary["run"]["platform"], "nangate45")
            self.assertEqual(summary["run"]["openroad_revision"], "feedface")
            self.assertEqual(summary["run"]["orfs_revision"], "cafebabe")
            self.assertEqual(summary["run"]["target_period_ns"], 10.0)
            self.assertEqual(summary["timing"]["setup_wns_ns"], -1.25)
            self.assertEqual(summary["power"]["internal"], 0.0020)
            self.assertEqual(summary["power"]["units"], "W")
            self.assertEqual(summary["quality"]["drc_errors"], 0)
            self.assertEqual(summary["quality"]["routing_overflow"], 0)

    def test_extracts_complete_summary_and_marks_power_estimated(self):
        metrics = {
            "run__flow__platform": "nangate45",
            "run__flow__openroad_commit": "abc123",
            "finish__design__instance__area": 1234.5,
            "finish__design__instance__count__stdcell": 321,
            "finish__timing__setup__ws": 1.25,
            "finish__timing__setup__tns": 0.0,
            "finish__power__total": 0.0042,
            "finish__power__internal": 0.0020,
            "finish__power__switching": 0.0015,
            "finish__power__leakage": 0.0007,
            "detailedroute__route__drc_errors": 0,
            "detailedroute__antenna__violating__nets": 0,
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            metrics_path = root / "metrics.json"
            output_json = root / "summary.json"
            output_md = root / "summary.md"
            metrics_path.write_text(json.dumps(metrics), encoding="utf-8")
            routing_log = self._write_routing_log(root)

            result = main([
                "--metrics", str(metrics_path),
                "--routing-log", str(routing_log),
                "--output-json", str(output_json),
                "--output-md", str(output_md),
                "--dfpvu-revision", "deadbeef",
                "--openroad-revision", "feedface",
                "--orfs-revision", "cafebabe",
                "--target-period-ns", "10.0",
            ])

            self.assertEqual(result, 0)
            self.assertTrue(output_json.exists())
            self.assertTrue(output_md.exists())
            summary = json.loads(output_json.read_text(encoding="utf-8"))
            self.assertEqual(summary["status"], "complete")
            self.assertEqual(summary["run"]["platform"], "nangate45")
            self.assertEqual(summary["run"]["dfpvu_revision"], "deadbeef")
            self.assertEqual(summary["run"]["openroad_revision"], "feedface")
            self.assertEqual(summary["run"]["orfs_revision"], "cafebabe")
            self.assertEqual(summary["run"]["target_period_ns"], 10.0)
            self.assertEqual(summary["power"]["total"], 0.0042)
            self.assertEqual(summary["power"]["units"], "W")
            self.assertIn("default activity", summary["run"]["power_activity"].lower())
            markdown = output_md.read_text(encoding="utf-8")
            self.assertIn("0.0042", markdown)
            self.assertIn("Target period (ns): 10.0", markdown)
            self.assertIn("Total (W): 0.0042", markdown)
            self.assertIn("default activity", markdown.lower())

    def test_rejects_incomplete_metrics_without_writing_success_summary(self):
        metrics = {
            "run__flow__platform": "nangate45",
            "finish__design__instance__area": 1234.5,
            "finish__design__instance__count__stdcell": 321,
            "finish__timing__setup__ws": 1.25,
            "finish__timing__setup__tns": 0.0,
            "finish__power__internal": 0.0020,
            "finish__power__switching": 0.0015,
            "finish__power__leakage": 0.0007,
            "detailedroute__route__drc_errors": 0,
            "detailedroute__antenna__violating__nets": 0,
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            metrics_path = root / "metrics.json"
            output_json = root / "summary.json"
            output_md = root / "summary.md"
            metrics_path.write_text(json.dumps(metrics), encoding="utf-8")
            routing_log = self._write_routing_log(root)
            result = main([
                "--metrics", str(metrics_path),
                "--routing-log", str(routing_log),
                "--output-json", str(output_json),
                "--output-md", str(output_md),
                "--dfpvu-revision", "deadbeef",
                "--openroad-revision", "feedface",
                "--orfs-revision", "cafebabe",
                "--target-period-ns", "10.0",
            ])
            self.assertEqual(result, 2)
            self.assertFalse(output_json.exists())
            self.assertFalse(output_md.exists())

    def test_rejects_nonzero_physical_quality_metrics(self):
        base_metrics = {
            "run__flow__platform": "nangate45",
            "finish__design__instance__area": 1234.5,
            "finish__design__instance__count__stdcell": 321,
            "finish__timing__setup__ws": 1.25,
            "finish__timing__setup__tns": 0.0,
            "finish__power__total": 0.0042,
            "finish__power__internal": 0.0020,
            "finish__power__switching": 0.0015,
            "finish__power__leakage": 0.0007,
            "detailedroute__route__drc_errors": 0,
            "detailedroute__antenna__violating__nets": 0,
        }
        cases = (
            ("DRC errors", "detailedroute__route__drc_errors", 1, 0),
            ("antenna-violating nets", "detailedroute__antenna__violating__nets", 1, 0),
            ("routing overflow", None, None, 1),
        )
        for expected_error, metric_name, metric_value, overflow in cases:
            with self.subTest(expected_error=expected_error):
                metrics = dict(base_metrics)
                if metric_name is not None:
                    metrics[metric_name] = metric_value
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    metrics_path = root / "metrics.json"
                    output_json = root / "summary.json"
                    output_md = root / "summary.md"
                    metrics_path.write_text(json.dumps(metrics), encoding="utf-8")
                    routing_log = self._write_routing_log(root, overflow)
                    result = main([
                        "--metrics", str(metrics_path),
                        "--routing-log", str(routing_log),
                        "--output-json", str(output_json),
                        "--output-md", str(output_md),
                        "--dfpvu-revision", "deadbeef",
                        "--openroad-revision", "feedface",
                        "--orfs-revision", "cafebabe",
                        "--target-period-ns", "10.0",
                    ])
                    self.assertEqual(result, 2)
                    self.assertFalse(output_json.exists())
                    self.assertFalse(output_md.exists())


if __name__ == "__main__":
    unittest.main()
