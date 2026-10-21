import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lab_host import (  # noqa: E402
    ROOT,
    _defaults_files_for_lab_build,
    _print_pre_reset_operator_briefing,
    _prepare_lab_build_directory,
    _refresh_stale_sdkconfig,
    _sdkconfig_missing_assignments,
    capture_lab,
    clean_lab,
    revalidate_run_target,
    resolve_run_target,
    RunTarget,
)
from deployment_config import Deployment, DeploymentMeta, Endpoint, Kit, SecretsProfile
from device_targets import Port
from lab_registry import get_lab, resolve_variant
from project_config import ProjectConfig, ProjectConfigError
from lab_adapter_lab02 import managed_canonical_plan, scripted_variant_name


class Lab02MakeVariantTests(unittest.TestCase):
    def test_scripted_variant_name_follows_backend(self):
        self.assertEqual(scripted_variant_name("managed-http"), "scripted-phaseb")
        self.assertEqual(scripted_variant_name("local"), "scripted")

    def test_managed_canonical_plan_maps_default_and_scripted(self):
        self.assertEqual(managed_canonical_plan("recycle", "managed-http"), ("manual", "phaseb"))
        self.assertEqual(managed_canonical_plan("phaseb", "managed-http"), ("manual", "phaseb"))
        self.assertEqual(
            managed_canonical_plan("scripted-phaseb", "managed-http"),
            ("scripted", "scripted-phaseb"),
        )
        self.assertIsNone(managed_canonical_plan("scripted", "managed-http"))
        self.assertIsNone(managed_canonical_plan("phaseb", "local"))


class LabOperatorResetBannerTests(unittest.TestCase):
    def test_registered_instructions_are_shown_at_flash_reset_for_each_lab(self):
        for name in ("lab02", "lab03", "lab04", "lab05", "lab05peak", "lab06", "lab07", "lab10"):
            spec = get_lab(name)
            variant = spec.variants[spec.default_variant]
            output = StringIO()
            with self.subTest(lab=name), redirect_stdout(output):
                _print_pre_reset_operator_briefing(ProjectConfig(), ROOT / variant.build_dir)
            self.assertIn(f"OPERATOR STEPS BEFORE RESET: {spec.lab_id}", output.getvalue())
            self.assertIn("After upload, wait", output.getvalue())
            self.assertIn(spec.run_instructions.splitlines()[0].strip(), output.getvalue())

    def test_lab06_manual_window_is_short_and_other_defaults_are_unchanged(self):
        self.assertEqual(get_lab("lab06").capture_seconds_default, 30)
        self.assertEqual(get_lab("lab07").capture_seconds_default, 90)
        self.assertEqual(get_lab("lab02").capture_seconds_default, 120)
        self.assertEqual(get_lab("lab10").capture_seconds_default, 180)

    def test_local_backend_maps_managed_default_to_local_fixture(self):
        self.assertEqual(resolve_variant(get_lab("lab02"), None).name, "phaseb")
        # The local comparison is selected by BACKEND=local in lab_host; its
        # implementation variant is the embedded bounded-inbox image.
        self.assertEqual(resolve_variant(get_lab("lab02"), "recycle").name, "recycle")


class LabHostSdkconfigTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.temp_path = Path(self.temp.name)

    def test_missing_assignments_detects_stale_sdkconfig(self):
        with self.subTest("empty sdkconfig"):
            sdk = self.temp_path / "sdkconfig"
            sdk.write_text("# empty\n", encoding="utf-8")
            defaults = self.temp_path / "defaults"
            defaults.write_text("CONFIG_LAB_AUTO_HARNESS=y\n", encoding="utf-8")
            missing = _sdkconfig_missing_assignments(sdk, [defaults])
            self.assertEqual(missing, ["CONFIG_LAB_AUTO_HARNESS=y"])

        with self.subTest("satisfied sdkconfig"):
            sdk.write_text("CONFIG_LAB_AUTO_HARNESS=y\n", encoding="utf-8")
            missing = _sdkconfig_missing_assignments(sdk, [defaults])
            self.assertEqual(missing, [])

    def test_prepare_unconfigured_build_dir_is_empty(self):
        build = self.temp_path / "build_prepare"
        build.mkdir(exist_ok=True)
        (build / "CMakeFiles").mkdir()
        _prepare_lab_build_directory(build)
        self.assertTrue(build.is_dir())
        self.assertEqual([], list(build.iterdir()))

    def test_clean_lab_removes_build_dir(self):
        config = ProjectConfig()
        spec = get_lab("lab02")
        var = resolve_variant(spec, "phaseb")
        with tempfile.TemporaryDirectory() as temp, patch("lab_host.ROOT", Path(temp)):
            build_dir = Path(temp) / var.build_dir
            build_dir.parent.mkdir(parents=True, exist_ok=True)
            build_dir.mkdir(exist_ok=True)
            (build_dir / "marker").write_text("x", encoding="utf-8")
            clean_lab(config, "lab02", "phaseb")
            self.assertFalse(build_dir.exists())

    def test_phaseb_build_requires_kit(self):
        config = ProjectConfig()
        spec = get_lab("lab02")
        var = resolve_variant(spec, "phaseb")
        build_dir = self.temp_path / "build_dir_phaseb"
        build_dir.mkdir(exist_ok=True)
        with self.assertRaisesRegex(ProjectConfigError, "Managed HTTP build requires KIT"):
            _defaults_files_for_lab_build(config, spec, var, build_dir, "", "")

    def test_phaseb_build_requires_private_wifi_before_url_resolution(self):
        config = ProjectConfig()
        spec = get_lab("lab02")
        var = resolve_variant(spec, "phaseb")
        kit = Kit("A", "endpoint-a", "profile-a", "SERIAL-A")
        deployment = Deployment(
            Path("local.yaml"), "local", DeploymentMeta("desk", "Desk"), (),
            (Endpoint("endpoint-a", "Endpoint A", "", ""),), (kit,), {},
        )
        target = RunTarget("A", "/dev/a", "SERIAL-A", kit, deployment)
        with patch("lab_host.load_lab02_workflow_config", side_effect=AssertionError("URL resolved before Wi-Fi preflight")):
            with self.assertRaisesRegex(ProjectConfigError, "per-kit Wi-Fi credentials"):
                _defaults_files_for_lab_build(
                    config, spec, var, self.temp_path, "A", "", target
                )

    def test_ensure_lab_built_always_runs_idf_build(self):
        from lab_host import ensure_lab_built

        config = ProjectConfig()
        with patch("lab_host.get_lab") as get_lab_mock, patch(
            "lab_host.resolve_variant"
        ) as resolve_variant, patch(
            "lab_host._defaults_files_for_lab_build", return_value=[]
        ), patch(
            "lab_host._refresh_stale_sdkconfig", return_value=False
        ), patch("lab_host.build_lab", return_value=self.temp_path) as build:
            get_lab_mock.return_value = type("Spec", (), {"project_dir": Path("client/labs/lab-14-device-ui-catalog")})()
            resolve_variant.return_value = type("Var", (), {"build_dir": Path("build/labs/lab-14")})()
            ensure_lab_built(config, "lab14", "manual")
        build.assert_called_once()

    def test_ensure_build_uses_same_frozen_defaults(self):
        from lab_host import ensure_lab_built

        config = ProjectConfig()
        spec = get_lab("lab02")
        variant = resolve_variant(spec, "phaseb")
        frozen = [self.temp_path / "phaseb.defaults"]
        with patch("lab_host.ROOT", self.temp_path), patch(
            "lab_host._defaults_files_for_lab_build", return_value=frozen
        ) as resolve_defaults, patch(
            "lab_host._refresh_stale_sdkconfig", return_value=True
        ), patch("lab_host.build_lab", return_value=self.temp_path) as build:
            ensure_lab_built(config, "lab02", "phaseb", kit="A")
        resolve_defaults.assert_called_once()
        self.assertIs(build.call_args.kwargs["frozen_defaults_files"], frozen)

    def test_refresh_removes_stale_sdkconfig(self):
        build = self.temp_path / "build_dir"
        build.mkdir()
        sdk = build / "sdkconfig"
        sdk.write_text("CONFIG_IDF_TARGET_ESP32S3=y\n", encoding="utf-8")
        defaults = self.temp_path / "lab.defaults"
        defaults.write_text(
            "CONFIG_LAB02_FIXTURE_16=y\nCONFIG_LAB_AUTO_HARNESS=y\n", encoding="utf-8"
        )
        removed = _refresh_stale_sdkconfig(build, [defaults])
        self.assertTrue(removed)
        self.assertFalse(sdk.is_file())

    def tearDown(self):
        pass


class LabHostTargetSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.temp_path = Path(self.temp.name)
        self.kit_a = Kit("A", "endpoint-a", "profile-a", "SERIAL-A")
        self.kit_b = Kit("B", "endpoint-b", "profile-b", "SERIAL-B")
        self.deployment = Deployment(
            Path("local.yaml"), "local", DeploymentMeta("test", "test"), (),
            (Endpoint("endpoint-a", "Endpoint A", "", ""), Endpoint("endpoint-b", "Endpoint B", "", "")),
            (self.kit_a, self.kit_b), {"desk": "endpoint-a"},
            {"profile-a": SecretsProfile("ssid-a", "password-a"), "profile-b": SecretsProfile("ssid-b", "password-b")},
        )
        self.config = ProjectConfig()

    def _resolve(self, available, kit="", port=""):
        with patch("lab_host.load_active_deployment", return_value=self.deployment), patch(
            "lab_host.ports", return_value=available
        ):
            return resolve_run_target(self.config, kit, port)

    def test_no_kit_requires_exactly_one_connected_registered_target(self):
        with self.assertRaisesRegex(ProjectConfigError, "exactly one"):
            self._resolve([])
        target = self._resolve([Port("/dev/a", "serial-a")])
        self.assertEqual((target.kit_id, target.device), ("A", "/dev/a"))
        with self.assertRaisesRegex(ProjectConfigError, "exactly one"):
            self._resolve([Port("/dev/a", "serial-a"), Port("/dev/b", "serial-b")])

    def test_explicit_kit_requires_connected_registered_binding_and_matching_port(self):
        target = self._resolve([Port("/dev/a", "serial-a")], kit="A")
        self.assertEqual(target.serial, "serial-a")
        with self.assertRaisesRegex(ProjectConfigError, "PORT does not match"):
            self._resolve(
                [Port("/dev/a", "serial-a"), Port("/dev/b", "serial-b")],
                kit="A",
                port="/dev/b",
            )
        target = self._resolve(
            [Port("/dev/a", "serial-a"), Port("/dev/b", "serial-b")], kit="A"
        )
        self.assertEqual((target.kit_id, target.device), ("A", "/dev/a"))
        with self.assertRaisesRegex(ProjectConfigError, "expected one connected USB match, found 0"):
            self._resolve([Port("/dev/a", "")], kit="A")
        with self.assertRaisesRegex(ProjectConfigError, "Unknown or ambiguous kit"):
            self._resolve([Port("/dev/a", "serial-a")], kit="unknown")
        target = self._resolve(
            [Port("/dev/a", "serial-a"), Port("/dev/b", "serial-b")], port="/dev/b"
        )
        self.assertEqual((target.kit_id, target.device), ("B", "/dev/b"))
        with self.assertRaisesRegex(ProjectConfigError, "readable USB serial"):
            self._resolve([Port("/dev/a", "")], port="/dev/a")

    def test_duplicate_serials_and_duplicate_roster_bindings_fail_closed(self):
        with self.assertRaisesRegex(ProjectConfigError, "expected one connected USB match, found 2"):
            self._resolve([Port("/dev/a", "serial-a"), Port("/dev/clone", "SERIAL-A")], kit="A")
        duplicate = Deployment(
            self.deployment.path, self.deployment.source, self.deployment.meta, (), (),
            (self.kit_a, Kit("C", "endpoint-c", "profile-c", "serial-a")), {},
        )
        with patch("lab_host.load_active_deployment", return_value=duplicate), patch(
            "lab_host.ports", return_value=[Port("/dev/a", "serial-a")]
        ), self.assertRaisesRegex(ProjectConfigError, "Duplicate USB serial"):
            resolve_run_target(self.config, "A", "")

    def test_frozen_target_detects_disconnect_or_swapped_device_identity(self):
        target = self._resolve([Port("/dev/a", "serial-a")], kit="A")
        with patch("lab_host.load_active_deployment", return_value=self.deployment), patch(
            "lab_host.ports", return_value=[]
        ), self.assertRaisesRegex(ProjectConfigError, "found 0"):
            revalidate_run_target(self.config, target)
        with patch("lab_host.load_active_deployment", return_value=self.deployment), patch(
            "lab_host.ports", return_value=[Port("/dev/other", "serial-a")]
        ), self.assertRaisesRegex(ProjectConfigError, "disconnected or changed"):
            revalidate_run_target(self.config, target)

    def test_build_only_kit_selection_does_not_enumerate_hardware(self):
        from lab_host import _defaults_files_for_lab_build

        spec = get_lab("lab02")
        var = resolve_variant(spec, "phaseb")
        target = RunTarget("A", "/dev/a", "serial-a", self.kit_a, self.deployment)
        with patch("lab_host.ports", side_effect=AssertionError("build must not enumerate USB")), patch(
            "lab_host.load_lab02_workflow_config",
            return_value=type("Workflow", (), {"client_url": "http://10.0.0.2:8765", "fixture_size_default": 64,
                                                "scenario_default": "nominal"})(),
        ), patch(
            "lab_host.write_deployment_kit_sdkconfig", return_value=None
        ) as write_overlay:
            files = _defaults_files_for_lab_build(
                self.config, spec, var, self.temp_path, "A", "", target
            )
        write_overlay.assert_called_once_with(
            self.temp_path, self.kit_a, self.deployment, "http://10.0.0.2:8765",
            fixture_size=64, scenario="nominal", scripted=False, build_id=""
        )
        self.assertIsInstance(files, list)

    def test_run_entrypoint_resolves_target_before_build_or_flash(self):
        from lab_host import main

        events = []
        target = RunTarget("A", "/dev/a", "serial-a", self.kit_a, self.deployment)
        config = self.config
        with patch("lab_host.ROOT", self.temp_path), patch(
            "lab_host.sys.argv", ["lab_host.py", "run", "lab02", "--flash"]
        ), patch(
            "lab_host.ProjectConfig", return_value=config
        ), patch("lab_host.run_canonical", side_effect=lambda *args, **kwargs: events.append("canonical") or 0) as run:
            self.assertEqual(main(), 0)
        self.assertEqual(events, ["canonical"])
        self.assertEqual(run.call_args.kwargs["mode"], "manual")
        self.assertEqual(run.call_args.kwargs["build_variant"], "phaseb")

    def test_lab02_capture_uses_generic_serial_path_not_canonical_runner(self):
        from lab_host import main

        target = RunTarget("A", "/dev/a", "serial-a", self.kit_a, self.deployment)
        with patch(
            "lab_host.sys.argv",
            ["lab_host.py", "capture", "lab02", "--no-flash", "--run-id", "capture-test"],
        ), patch("lab_host.ProjectConfig", return_value=self.config), patch(
            "lab_host.resolve_run_target", return_value=target
        ), patch("lab_host.capture_lab", return_value=self.temp_path / "serial.txt") as capture, patch(
            "lab_host.run_canonical"
        ) as canonical, patch("lab_host.ensure_lab_built") as build, patch(
            "lab_host.load_lab02_workflow_config"
        ) as workflow:
            self.assertEqual(main(), 0)

        canonical.assert_not_called()
        build.assert_not_called()
        workflow.assert_not_called()
        capture.assert_called_once_with(
            self.config,
            "lab02",
            kit="A",
            port="/dev/a",
            variant=None,
            seconds=120,
            flash=False,
            run_id="capture-test",
            action="capture",
            emit_operator_help=True,
            target=target,
        )

    def test_lab02_scripted_and_eager_runs_stay_on_canonical_runner(self):
        from lab_host import main

        target = RunTarget("A", "/dev/a", "serial-a", self.kit_a, self.deployment)
        for variant in ("scripted-phaseb", "eager"):
            with self.subTest(variant=variant), patch(
                "lab_host.ROOT", self.temp_path
            ), patch(
                "lab_host.sys.argv",
                ["lab_host.py", "run", "lab02", "--variant", variant, "--no-flash"],
            ), patch("lab_host.ProjectConfig", return_value=self.config), patch(
                "lab_host.run_canonical", return_value=0
            ) as canonical, patch("lab_host.capture_lab") as capture:
                self.assertEqual(main(), 0)

            canonical.assert_called_once()
            self.assertEqual(canonical.call_args.kwargs["build_variant"], variant)
            capture.assert_not_called()

    def test_phaseb_flash_refreshes_dynamic_deployment_defaults(self):
        from lab_host import main

        target = RunTarget("A", "/dev/a", "serial-a", self.kit_a, self.deployment)
        with patch(
            "lab_host.sys.argv",
            ["lab_host.py", "flash", "lab02", "--variant", "phaseb", "--kit", "A"],
        ), patch("lab_host.ProjectConfig", return_value=self.config), patch(
            "lab_host.resolve_run_target", return_value=target
        ), patch("lab_host.ensure_lab_built", return_value=self.temp_path) as ensure, patch(
            "lab_host.revalidate_run_target", return_value="/dev/a"
        ), patch("lab_host.flash_lab_build") as flash:
            self.assertEqual(main(), 0)
        ensure.assert_called_once_with(
            self.config,
            "lab02",
            "phaseb",
            kit="A",
            port="/dev/a",
            target=target,
        )
        flash.assert_called_once_with(self.config, self.temp_path, "/dev/a")

    def test_capture_revalidates_frozen_identity_before_flash_write(self):
        target = RunTarget("A", "/dev/a", "serial-a", self.kit_a, self.deployment)
        with patch("lab_host.print_run_start"), patch("lab_host.print_operator_help"), patch(
            "lab_host.revalidate_run_target", side_effect=ProjectConfigError("disconnected")
        ) as revalidate, patch("lab_host.flash_lab_build") as flash:
            with self.assertRaisesRegex(ProjectConfigError, "disconnected"):
                capture_lab(
                    self.config,
                    "lab02",
                    kit="A",
                    port="/dev/a",
                    variant="phaseb",
                    seconds=1,
                    flash=True,
                    run_id="test-run",
                    emit_operator_help=False,
                    target=target,
                )
        revalidate.assert_called_once_with(self.config, target)
        flash.assert_not_called()

    def test_capture_accepts_alias_for_frozen_target_with_or_without_supplied_target(self):
        target = RunTarget("A", "/dev/a", "serial-a", self.kit_a, self.deployment)
        for supplied_target in (None, target):
            with self.subTest(supplied_target=supplied_target is not None), patch(
                "lab_host.print_run_start"
            ), patch("lab_host.print_operator_help"), patch(
                "lab_host.resolve_run_target", return_value=target
            ) as resolve, patch(
                "lab_host.capture_serial",
                side_effect=lambda _device, serial_path, _seconds: serial_path.parent.mkdir(
                    parents=True, exist_ok=True
                ),
            ) as capture_serial, patch(
                "lab_host.ProjectConfig.path_value", return_value=self.temp_path
            ), patch("lab_host.parse_serial_file"):
                capture_lab(
                    self.config,
                    "lab02",
                    kit="desk",
                    port="/dev/a",
                    variant="phaseb",
                    seconds=1,
                    flash=False,
                    run_id="test-run",
                    emit_operator_help=False,
                    target=supplied_target,
                )
            if supplied_target is None:
                resolve.assert_called_once_with(self.config, "desk", "/dev/a")
            else:
                resolve.assert_not_called()
            capture_serial.assert_called_once()
            self.assertEqual((capture_serial.call_args.args[0], capture_serial.call_args.args[2]), ("/dev/a", 1))

    def test_capture_alias_mismatch_fails_before_flash_write(self):
        target = RunTarget("A", "/dev/a", "serial-a", self.kit_a, self.deployment)
        with patch("lab_host.print_run_start"), patch("lab_host.print_operator_help"), patch(
            "lab_host.revalidate_run_target"
        ) as revalidate, patch("lab_host.flash_lab_build") as flash:
            with self.assertRaisesRegex(ProjectConfigError, "does not match requested KIT"):
                capture_lab(
                    self.config,
                    "lab02",
                    kit="B",
                    port="/dev/a",
                    variant="phaseb",
                    seconds=1,
                    flash=True,
                    run_id="test-run",
                    emit_operator_help=False,
                    target=target,
                )
        revalidate.assert_not_called()
        flash.assert_not_called()


if __name__ == "__main__":
    unittest.main()
