"""Default adapter for labs that need no experiment-specific host workflow."""
from __future__ import annotations

from lab_runner import RunHooks, run_with_adapter


class Adapter:
    def run(self, host, config, spec, args, *, mode: str) -> int:
        variant = args.variant or (mode if mode in spec.variants else spec.default_variant)
        seconds = args.seconds if args.seconds is not None else spec.capture_seconds_default
        flash = host._flash_from_env_and_args(args, spec, for_run=True)

        def resolve_target():
            return host.resolve_run_target(config, args.kit, args.port)

        def build(target):
            if getattr(args, "no_build", False):
                return host.ROOT / host.resolve_variant(spec, variant).build_dir
            return host.ensure_lab_built(
                config, spec.make_name, variant,
                kit=target.kit_id, port=target.device, target=target,
            )

        def capture(target, rid):
            return host.capture_lab(
                config, spec.make_name, kit=target.kit_id, port=target.device,
                variant=variant, seconds=seconds, flash=flash,
                run_id=rid, action="run", emit_operator_help=True, target=target,
            )

        return run_with_adapter(
            repo_root=host.ROOT, lab_id=spec.lab_id, evidence_slug=spec.evidence_slug,
            variant=variant, mode=mode, requested_run_id=args.run_id,
            hooks=RunHooks(resolve_target, build, capture),
            project_path=str(spec.project_dir), firmware_target=spec.target,
            capture_seconds=seconds,
        )
