#!/usr/bin/env python3
"""Exercise the lavapipe preflight and golden harness without rendering."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ICD = "/usr/share/vulkan/icd.d/lvp_icd.json"


class LavapipeTests(unittest.TestCase):
    def pin(self, path):
        return subprocess.run(
            [
                "bash",
                "-eu",
                "-c",
                'source tools/lavapipe.sh; pin_lavapipe "$1"',
                "test",
                str(path),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_missing_icd_fails_with_reinstall_advice(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "missing-lvp_icd.json"
            result = self.pin(missing)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn(str(missing), result.stderr)
        self.assertIn("Infra Lead must reinstall mesa-vulkan-drivers", result.stderr)

    def test_directory_is_not_an_icd(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.pin(directory)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn(directory, result.stderr)

    def test_golden_overrides_conflicting_loader_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tools = root / "tools"
            tools.mkdir()
            for name in ("golden.sh", "lavapipe.sh"):
                shutil.copy2(ROOT / "tools" / name, tools / name)
            # Capture is a dependency of the harness under test. Record its
            # environment and produce a frame without starting a renderer.
            capture = tools / "capture.sh"
            capture.write_text(
                "#!/usr/bin/env bash\nset -eu\n"
                'printf "%s\\n%s\\n" "$VK_ICD_FILENAMES" "$VK_DRIVER_FILES" > loader-env\n'
                'printf frame > "$1"\n'
            )
            capture.chmod(0o755)
            result = subprocess.run(
                ["bash", "tools/golden.sh", "--update"],
                cwd=root,
                env={
                    **os.environ,
                    "VK_ICD_FILENAMES": "/conflicting/intel.json",
                    "VK_DRIVER_FILES": "/conflicting/other.json",
                },
                capture_output=True,
                text=True,
                check=False,
            )
            if not Path(ICD).is_file() or not os.access(ICD, os.R_OK):
                # Light CI need not have a renderer installed. Exercise the
                # harness preflight there rather than skipping the test.
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn(ICD, result.stderr)
                self.assertIn(
                    "Infra Lead must reinstall mesa-vulkan-drivers", result.stderr
                )
                self.assertFalse((root / "loader-env").exists())
                return
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((root / "loader-env").read_text().splitlines(), [ICD, ICD])
            self.assertEqual(
                (root / "tests/golden/opening-seed20.png").read_bytes(), b"frame"
            )


if __name__ == "__main__":
    unittest.main()
