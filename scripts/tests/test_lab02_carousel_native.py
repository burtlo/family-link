"""C0 characterization against production C and the vendored LVGL renderer.

Run directly with --artifact-dir to retain native traces and PPM frames. These
baseline assertions preserve remaining defects; queue pressure is a C1a acceptance gate.
"""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-02-bounded-inbox"
SCENARIOS = ("fast-boundary", "delayed-focus", "reversal", "page-during-snap",
             "held-page", "push-during-snap", "remove-during-snap", "stale-after-revision", "queue-full", "failed-page", "pool-bounds")


def inputs():
    config = configparser.ConfigParser()
    config.read(ROOT / "config/host.defaults.ini")
    cjson = Path(os.environ.get("IDF_PATH", config.get("idf", "path"))).expanduser() / "components/json/cJSON"
    lvgl = LAB / "managed_components/lvgl__lvgl"
    if not shutil.which("cmake") or not shutil.which("cc") or not (cjson / "cJSON.c").is_file() or not (lvgl / "CMakeLists.txt").is_file():
        raise unittest.SkipTest("C0 needs cmake, host cc, ESP-IDF cJSON and vendored LVGL")
    return cjson, lvgl


def run_characterization(artifact_dir, slow_bind_ms=0):
    cjson, lvgl = inputs()
    artifact_dir = Path(artifact_dir).resolve()
    artifact_dir.mkdir(parents=True, exist_ok=True)
    source_files = set()
    for directory in (LAB / "components/lab02", LAB / "tests", LAB / "fixtures", lvgl):
        source_files.update(p for p in directory.rglob("*") if p.is_file())
    source_files.update((cjson / "cJSON.c", cjson / "cJSON.h", Path(__file__).resolve()))
    manifest = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(source_files)}
    compiler = subprocess.run(["cc", "--version"], check=True, capture_output=True, text=True).stdout
    identity = hashlib.sha256((json.dumps(manifest, sort_keys=True) + compiler + str(slow_bind_ms)).encode()).hexdigest()
    cache = Path(tempfile.gettempdir()) / ("family-link-lab02-c0-" + identity[:20])
    (artifact_dir / "source-sha256.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (artifact_dir / "runtime.json").write_text(json.dumps({"input_digest": identity, "cache": str(cache),
        "compiler": compiler, "evidence": "L2 headless LVGL; transport and RTOS adapters; no target evidence"}, indent=2) + "\n")
    commands = []

    def execute(argv, log, timeout):
        commands.append(argv)
        (artifact_dir / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        (artifact_dir / log).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise AssertionError(f"C0 command exited {result.returncode}; see {artifact_dir / log}")
        return result.stdout

    execute(["cmake", "-S", str(LAB / "tests/c0_runtime"), "-B", str(cache),
             "-DCJSON_DIR=" + str(cjson), "-DLAB02_SLOW_BIND_MS=" + str(slow_bind_ms)], "configure-final.log", 60)
    execute(["cmake", "--build", str(cache), "-j", "8"], "build-final.log", 180)
    for scenario in (SCENARIOS + (("slow-bind",) if slow_bind_ms else ())):
        output = execute([str(cache / "lab02_c0"), scenario, str(artifact_dir)], scenario + ".log", 10)
        if f"C0_RESULT scenario={scenario} characterization=PASS acceptance=F001-C2" not in output:
            raise AssertionError(f"missing native result for {scenario}")
    for scenario in ("retry-success", "retry-boot-fail", "retry-background-fail",
                     "retry-reset-pending", "retry-reset-admitted", "retry-retained-worker"):
        output = execute([str(cache / "lab02_initial_retry"), scenario, str(artifact_dir)],
                         scenario + ".log", 10)
        if f"F008_RUNTIME scenario={scenario} PASS" not in output:
            raise AssertionError(f"missing actual click/provider result for {scenario}")
    for scenario in ("surviving-displacement", "removed-next", "removed-last", "removed-unknown",
                     "superseded", "new-revision", "failure-retry", "queue-pressure",
                     "admission-busy", "unmaterialized-intent", "incomplete-retry", "late-result", "timeout-retry"):
        output = execute([str(cache / "lab02_identity"), scenario, str(artifact_dir)],
                         "identity-" + scenario + ".log", 10)
        if f"F009_RUNTIME scenario={scenario} PASS" not in output:
            raise AssertionError(f"missing production identity result for {scenario}")
    # Inspect actual renderer output pixels, not a Python behavior model.
    pixels = (artifact_dir / "fast-boundary-drag20.ppm").read_bytes().split(b"\n", 3)[3]
    card_rgb = bytes.fromhex("1c2228")
    card_pixels = sum(pixels[i:i + 3] == card_rgb for i in range(0, len(pixels), 3))
    if card_pixels == 0:
        raise AssertionError("continuous traversal lost all visible card surfaces")
    (artifact_dir / "pixel-observations.json").write_text(json.dumps({
        "fast-boundary-drag20": {"card_background_pixels": card_pixels,
                                 "observed": "continuous card surfaces in rendered viewport"}}, indent=2) + "\n")
    return artifact_dir


class Lab02CarouselNativeTests(unittest.TestCase):
    def test_c0_characterization(self):
        location = os.environ.get("LAB02_C0_ARTIFACT_DIR")
        if location:
            run_characterization(location)
        else:
            with tempfile.TemporaryDirectory(prefix="lab02-c0-evidence-") as tmp:
                run_characterization(tmp)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact-dir", required=True)
    parser.add_argument("--slow-bind-ms", type=int, default=0)
    args = parser.parse_args()
    print(run_characterization(args.artifact_dir, args.slow_bind_ms))
