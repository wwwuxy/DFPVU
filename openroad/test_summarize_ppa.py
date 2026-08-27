import json
import tempfile
import unittest
from pathlib import Path

from openroad.summarize_ppa import main


class SummarizePpaTest(unittest.TestCase):
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

            result = main([
                "--metrics", str(metrics_path),
                "--output-json", str(output_json),
                "--output-md", str(output_md),
                "--dfpvu-revision", "deadbeef",
            ])

            self.assertEqual(result, 0)
            self.assertTrue(output_json.exists())
            self.assertTrue(output_md.exists())
            summary = json.loads(output_json.read_text(encoding="utf-8"))
            self.assertEqual(summary["status"], "complete")
            self.assertEqual(summary["run"]["platform"], "nangate45")
            self.assertEqual(summary["run"]["dfpvu_revision"], "deadbeef")
            self.assertEqual(summary["power"]["total"], 0.0042)
            self.assertIn("default activity", summary["run"]["power_activity"].lower())
            markdown = output_md.read_text(encoding="utf-8")
            self.assertIn("0.0042", markdown)
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
            result = main(["--metrics", str(metrics_path), "--output-json", str(output_json), "--output-md", str(output_md), "--dfpvu-revision", "deadbeef"])
            self.assertEqual(result, 2)
            self.assertFalse(output_json.exists())
            self.assertFalse(output_md.exists())


if __name__ == "__main__":
    unittest.main()
