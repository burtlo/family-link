# Shared lab Kconfig

Every firmware lab under `client/labs/<slug>/` should **source** [`lab_common.projbuild`](lab_common.projbuild) from its root `Kconfig.projbuild` so operator-facing options use the same names.

| Symbol | menuconfig | Purpose |
|--------|------------|---------|
| `CONFIG_LAB_AUTO_HARNESS` | **Family Link device lab** → Auto-scroll inbox after boot | Device runs scripted traversal; serial `HARNESS_DONE` when done |

**Host (no menuconfig):** `make lab.<name>.scripted` uses [`sdkconfig.defaults.scripted`](sdkconfig.defaults.scripted) via the registry `scripted` build variant.

**Manual runs:** `make lab.<name>` (you scroll on the BOX during capture).

Standard: [`docs/standards/labs-configuration.md`](../../../docs/standards/labs-configuration.md).
