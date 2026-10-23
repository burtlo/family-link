"""Fixed host-code adapter allowlist; registry data never names import paths."""
from __future__ import annotations

import importlib
from typing import Protocol

from lab_registry import LabSpec, LabRegistryError


class LabRunAdapter(Protocol):
    def run(self, host, config, spec: LabSpec, args, *, mode: str) -> int | None: ...


_ADAPTER_MODULES = {
    "generic": "lab_runner_adapter",
    "lab02": "lab_adapter_lab02",
    "lab03": "lab_adapter_lab03",
    "lab04": "lab_adapter_lab04",
    "lab05peak": "lab_adapter_lab05peak",
    "lab06": "lab_adapter_lab06",
    "lab07": "lab_adapter_lab07",
    "lab08": "lab_adapter_lab08",
    "lab09": "lab_adapter_lab09",
    "lab10": "lab_adapter_lab10",
    "lab11": "lab_adapter_lab11",
    "lab12": "lab_adapter_lab12",
    "lab15": "lab_adapter_lab15",
    "lab16": "lab_adapter_lab16",
}


def resolve_adapter(spec: LabSpec) -> LabRunAdapter:
    """Resolve only a built-in key and import that adapter on demand."""
    module_name = _ADAPTER_MODULES.get(spec.adapter_key)
    if module_name is None:
        raise LabRegistryError(f"Unsupported adapter key for lab.{spec.make_name}")
    module = importlib.import_module(module_name)
    return module.Adapter()
