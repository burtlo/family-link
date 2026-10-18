import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def make_dry_run(*arguments: str) -> str:
    environment = os.environ.copy()
    for name in ("KIT", "PORT", "VARIANT"):
        environment.pop(name, None)
    result = subprocess.run(
        ["make", "--no-print-directory", "-n", *arguments],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"make dry-run failed ({result.returncode}): {result.stderr.strip()}"
        )
    return result.stdout


class LabMakeAliasTests(unittest.TestCase):
    def test_lab10_scripted_build_alias_forces_scripted_variant(self):
        output = make_dry_run(
            "lab.lab10.scripted.build", "KIT=box3-test", "VARIANT=manual"
        )
        self.assertIn(
            "scripts/lab_host.py build lab10 --variant scripted --kit \"box3-test\"",
            output,
        )
        self.assertNotIn("--variant manual", output)

    def test_lab10_standard_build_preserves_manual_variant_override(self):
        output = make_dry_run(
            "lab.lab10.build", "KIT=box3-test", "VARIANT=manual"
        )
        self.assertIn(
            "scripts/lab_host.py build lab10 --variant \"manual\"",
            output,
        )
        self.assertNotIn("--variant scripted", output)

    def test_lab10_standard_build_preserves_registry_default(self):
        output = make_dry_run("lab.lab10.build")
        self.assertIn("scripts/lab_host.py build lab10", output)
        self.assertNotIn("--variant", output)


if __name__ == "__main__":
    unittest.main()
