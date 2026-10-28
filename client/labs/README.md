# Client lab firmware (v2)

Per-lab ESP-IDF projects live here. This directory is **documentation-only** until the first lab is implemented.

**Canonical guide:** [`docs/labs/repository-layout.md`](../../docs/labs/repository-layout.md)  
**Lab index:** [`docs/labs/README.md`](../../docs/labs/README.md)

Expected first project: `lab-02-bounded-inbox/` (`LAB-02`).  
Implementation plan: [`docs/labs/lab-02-implementation-outline.md`](../../docs/labs/lab-02-implementation-outline.md).

**Shared Kconfig:** [`kconfig/lab_common.projbuild`](kconfig/lab_common.projbuild) (`CONFIG_LAB_AUTO_HARNESS`, etc.) — source from every lab’s `Kconfig.projbuild`.

**Host commands:** each implemented lab registers a short **make name** in [`config/labs.registry.yaml`](../../config/labs.registry.yaml) per [`docs/standards/labs-configuration.md`](../../docs/standards/labs-configuration.md) (e.g. `lab02` → `make lab.lab02`). Run `make lab.help` and `make config.labs` from the repo root. Add new labs by creating the ESP-IDF project under this directory and appending a registry entry; Makefile pattern rules apply automatically.
