#!/usr/bin/env python3
"""Check the real gate runner's build budget; replace only external gates.

BREAKWATER_CHECK_SCRIPT can select an earlier check.sh for red-first checks.
No builds run here; tools/check.sh still runs every real gate afterwards.
"""

import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class CheckParallelTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        tools = self.root / "tools"
        tools.mkdir()
        source = Path(
            os.environ.get(
                "BREAKWATER_CHECK_SCRIPT", Path(__file__).with_name("check.sh")
            )
        )
        self.check = tools / "check.sh"
        self.check.write_bytes(source.read_bytes())
        self.check.chmod(0o755)
        # Isolate shell orchestration from its dependencies, including this
        # self-check when invoked by check.sh. The runner itself stays intact.
        (tools / "test_check_parallel.py").write_text(
            "# external self-check dependency\n"
        )
        dependencies = set(
            re.findall(r"\btools/([a-z_]+\.sh)\b", source.read_text())
        ) - {"check.sh"}
        for name in sorted(dependencies):
            path = tools / name
            path.write_text("#!/usr/bin/env bash\nexit 0\n")
            path.chmod(0o755)
        self.bins = self.root / "bin"
        self.bins.mkdir()
        cmake = self.bins / "cmake"
        cmake.write_text(
            f"#!{sys.executable}\n"
            "import json, os, sys\n"
            "with open(os.environ['ARGV_LOG'], 'a') as f:\n"
            "    f.write(json.dumps(sys.argv[1:]) + '\\n')\n"
        )
        cmake.chmod(0o755)
        ctest = self.bins / "ctest"
        ctest.write_text("#!/usr/bin/env bash\nexit 0\n")
        ctest.chmod(0o755)

    def check_budget(self, value, expected):
        log = self.root / "argv.jsonl"
        env = dict(
            os.environ, PATH=f"{self.bins}:{os.environ['PATH']}", ARGV_LOG=str(log)
        )
        env.pop("CMAKE_BUILD_PARALLEL_LEVEL", None)
        env.pop("BREAKWATER_SKIP_GOLDEN", None)
        if value is not None:
            env["CMAKE_BUILD_PARALLEL_LEVEL"] = value
        result = subprocess.run(
            [str(self.check)], env=env, capture_output=True, text=True, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        builds = [
            json.loads(line)
            for line in log.read_text().splitlines()
            if json.loads(line)[0] == "--build"
        ]
        self.assertEqual(len(builds), 3, builds)
        self.assertEqual(
            {args[args.index("--preset") + 1] for args in builds},
            {"asan", "gcc", "dev"},
        )
        for args in builds:
            with self.subTest(args=args):
                self.assertEqual(args.count("--parallel"), 1, args)
                index = args.index("--parallel")
                self.assertLess(index + 1, len(args), args)
                self.assertEqual(args[index + 1 :], [expected], args)

    def test_unset_budget_defaults_to_two(self):
        self.check_budget(None, "2")

    def test_empty_budget_defaults_to_two(self):
        self.check_budget("", "2")

    def test_explicit_budget_is_respected(self):
        self.check_budget("1", "1")

    def test_whitespace_is_forwarded_as_one_argument(self):
        # CMake diagnoses invalid values. The shell must not split input into
        # extra options or commands before CMake can validate it.
        self.check_budget("1 2", "1 2")


if __name__ == "__main__":
    unittest.main()
