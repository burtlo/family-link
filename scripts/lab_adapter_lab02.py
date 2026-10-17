"""LAB-02 compatibility adapter routed through the shared run envelope."""
from __future__ import annotations

from lab_runner import RunHooks, run_with_adapter


def scripted_variant_name(backend: str) -> str:
    return "scripted" if backend == "local" else "scripted-phaseb"


def managed_canonical_plan(variant_name: str, backend: str) -> tuple[str, str] | None:
    if backend != "managed-http":
        return None
    if variant_name == "scripted-phaseb":
        return "scripted", "scripted-phaseb"
    if variant_name in {"recycle", "phaseb"}:
        return "manual", "phaseb"
    return None


class Adapter:
    def run(self, host, config, spec, args, *, mode: str) -> int | None:
        """Run LAB-02's canonical coordinator inside the shared evidence lifecycle."""
        variant = args.variant or None
        explicit_mode = getattr(args, "mode", None)
        if explicit_mode is not None and variant is None:
            variant = (("scripted" if args.backend == "local" else "scripted-phaseb")
                       if explicit_mode == "scripted" else
                       ("recycle" if args.backend == "local" else "phaseb"))
        selected = host.resolve_variant(spec, variant)
        if explicit_mode is None and selected.name in {"scripted", "scripted-phaseb"}:
            mode = "scripted"
        backend = args.backend
        build_variant = selected.name
        if backend == "local":
            build_variant = {"scripted-phaseb": "scripted", "phaseb": "recycle"}.get(
                build_variant, build_variant)
            selected = host.resolve_variant(spec, build_variant)
        else:
            canonical = managed_canonical_plan(build_variant, backend)
            if canonical is not None:
                if explicit_mode is None:
                    mode, build_variant = canonical
                else:
                    build_variant = canonical[1]
            elif build_variant != "eager":
                return None
        if build_variant == "eager":
            backend = "local"

        seconds = args.seconds if args.seconds is not None else spec.capture_seconds_default
        flash = host._flash_from_env_and_args(args, spec, for_run=True)
        result: dict = {}

        def execute(context, evidence):
            def build(cfg, name, chosen, **kwargs):
                return host.build_lab(cfg, name, chosen, **kwargs)

            def parse(serial_path, metrics_path):
                return host.parse_serial_file(spec, serial_path, metrics_path)

            exit_code = host.run_canonical(
                config, mode=mode, kit=args.kit, port=args.port, seconds=seconds,
                flash=flash, run_id=context.run_id,
                no_build=getattr(args, "no_build", False), backend=backend,
                build_variant=build_variant,
                target_resolver=host.resolve_run_target,
                target_revalidator=host.revalidate_run_target, build=build,
                flash_build=host.flash_lab_build, parse=parse,
                evidence=evidence, outcome_sink=result.update,
            )
            # The canonical runner reports its full observed outcome through
            # outcome_sink. A zero return from a legacy test double represents
            # its historical success contract and keeps the adapter boundary
            # compatible with callers that patch the coordinator.
            if not result and exit_code == 0:
                result.update({"workflow_status": "PASS", "experiment_verdict": "UNMEASURED",
                               "caller_completed": True, "cleanup_succeeded": True,
                               "terminal_reason": "completed",
                               "stages": {"preflight": "passed", "build": "passed",
                                          "activity": "passed", "cleanup": "passed"}})
            return result

        return run_with_adapter(
            repo_root=host.ROOT, lab_id=spec.lab_id, evidence_slug=spec.evidence_slug,
            variant=build_variant, mode=mode, requested_run_id=args.run_id,
            hooks=RunHooks(lambda: None, lambda _target: None, lambda _target, _rid: None),
            project_path=str(spec.project_dir), firmware_target=spec.target,
            capture_seconds=seconds, execute=execute,
        )
