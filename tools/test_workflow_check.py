#!/usr/bin/env python3
"""Exercise the real workflow-policy CLI with malformed and unsafe workflows."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import yaml


class WorkflowPolicyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.github = {
            "on": {
                "push": {"branches": ["main"]},
                "pull_request": None,
                "workflow_dispatch": None,
            },
            "concurrency": {
                "group": "${{ github.workflow }}-${{ github.ref }}",
                "cancel-in-progress": "${{ github.event_name == 'pull_request' }}",
            },
            "jobs": {
                "light": {
                    "runs-on": "ubuntu-24.04",
                    "timeout-minutes": 10,
                    "steps": [{"run": "true"}],
                }
            },
        }
        self.write(self.github)

    def write(self, data, forge="github"):
        path = self.root / f".{forge}/workflows/ci.yml"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(yaml.safe_dump(data, sort_keys=False))
        return path

    def run_guard(self):
        return subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("workflow_check.py")),
                "--root",
                str(self.root),
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def rejected(self, fragment):
        result = self.run_guard()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(fragment, result.stderr)

    def test_safe_main_pr_manual(self):
        result = self.run_guard()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("1 workflow(s)", result.stdout)

    def test_missing_concurrency_or_group(self):
        for concurrency in (
            {},
            {
                "group": "  ",
                "cancel-in-progress": "${{ github.event_name == 'pull_request' }}",
            },
            "shared",
            {
                "group": "shared",
                "cancel-in-progress": "${{ github.event_name == 'pull_request' }}",
            },
            {
                "group": "${{ github.workflow }}",
                "cancel-in-progress": "${{ github.event_name == 'pull_request' }}",
            },
        ):
            with self.subTest(concurrency=concurrency):
                self.github["concurrency"] = concurrency
                self.write(self.github)
                self.rejected("concurrency")

    def test_cancellation_must_preserve_main_and_manual(self):
        for cancellation in (
            True,
            False,
            "${{ github.ref != 'refs/heads/main' }}",
            None,
        ):
            with self.subTest(cancellation=cancellation):
                self.github["concurrency"]["cancel-in-progress"] = cancellation
                self.write(self.github)
                self.rejected("cancel-in-progress")

    def test_unbounded_branch_and_tag_pushes(self):
        for push in (
            None,
            {},
            {"branches": ["main", "feature"]},
            {"branches": ["*"]},
            {"branches-ignore": ["draft"]},
            {"branches": ["main"], "tags": ["v*"]},
            {"branches": ["main"], "tags-ignore": ["*"]},
        ):
            with self.subTest(push=push):
                self.github["on"]["push"] = push
                self.write(self.github)
                self.rejected("push")

    def test_unsupported_and_implicit_triggers(self):
        for events in ("pull_request", ["push", "pull_request"], {"schedule": []}, {}):
            with self.subTest(events=events):
                self.github["on"] = events
                self.write(self.github)
                self.rejected("workflow:")

    def test_heavy_or_unbounded_github_jobs(self):
        for timeout in (None, 0, 11, "${{ inputs.timeout }}"):
            with self.subTest(timeout=timeout):
                self.github["jobs"]["light"]["timeout-minutes"] = timeout
                self.write(self.github)
                self.rejected("timeout")
        self.github["jobs"]["heavy"] = self.github["jobs"]["light"].copy()
        self.write(self.github)
        self.rejected("exactly one light job")

    def test_duplicate_keys_and_malformed_yaml(self):
        for text in (
            "on: {}\non: {}\n",
            "jobs: [",
            "- workflow",
            "jobs:\n  ? [a, b]\n  : value\n",
        ):
            with self.subTest(text=text):
                self.write(self.github).write_text(text)
                self.rejected("workflow:")

    def test_missing_or_extra_github_workflow(self):
        path = self.write(self.github)
        extra = path.with_name("extra.yaml")
        extra.write_text(path.read_text())
        self.rejected("exactly one light workflow")
        extra.unlink()
        path.unlink()
        self.rejected("exactly one light workflow")

    def test_gitea_safe_and_unsafe_cases(self):
        gitea = {
            "on": {
                "push": {"branches": ["main", "ci-heavy"]},
                "workflow_dispatch": None,
            },
            "concurrency": {
                "group": "${{ gitea.workflow }}-${{ gitea.ref }}",
                "cancel-in-progress": "${{ gitea.event_name == 'push' && gitea.ref != 'refs/heads/main' }}",
            },
            "jobs": {
                "suite": {
                    "runs-on": "ci-build",
                    "timeout-minutes": 179,
                    "steps": [{"run": "tools/check.sh"}],
                }
            },
        }
        self.write(gitea, "gitea")
        self.assertEqual(self.run_guard().returncode, 0)
        for runner in ("ubuntu-latest", "ci-unknown", ["ci-build"]):
            with self.subTest(runner=runner):
                gitea["jobs"]["suite"]["runs-on"] = runner
                self.write(gitea, "gitea")
                self.rejected("unsupported runner")
        gitea["jobs"]["suite"]["runs-on"] = "ci-light"
        gitea["jobs"]["suite"]["timeout-minutes"] = 180
        self.write(gitea, "gitea")
        self.rejected("timeout")
        gitea["jobs"]["suite"]["timeout-minutes"] = 1
        gitea["on"]["pull_request"] = None
        self.write(gitea, "gitea")
        self.rejected("unsupported trigger")
        del gitea["on"]["pull_request"]
        for branch in ("feature/*", "release+", "!main", "release?", "branch[12]"):
            with self.subTest(branch=branch):
                gitea["on"]["push"]["branches"] = [branch]
                self.write(gitea, "gitea")
                self.rejected("named branches")


if __name__ == "__main__":
    unittest.main()
