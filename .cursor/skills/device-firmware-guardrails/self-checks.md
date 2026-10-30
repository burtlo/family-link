# Device firmware self-checks

Run these before reporting any `client/device/` work as done. Replace `<base>`
with the baseline commit from Step 0. Paste the output into your report; do not
summarize it as "passed".

Shell: on Windows these run in PowerShell. Chain with `;`, never `&&`.

## Commands when `make` is not on PATH

The Makefile only forwards to Python. Use the repo virtualenv directly
(Windows: `.venv\Scripts\python.exe`; macOS: `.venv/bin/python`).

| Make target | Direct equivalent |
|---|---|
| `make test` | `.venv\Scripts\python.exe -m unittest discover -s scripts/tests` |
| `make app:test` | `.venv\Scripts\python.exe scripts/app_test_suite.py` |
| `make app:build KIT=<k>` | `.venv\Scripts\python.exe scripts/app_host.py build --kit <k>` |
| `make app:flash KIT=<k>` | `.venv\Scripts\python.exe scripts/app_host.py flash --kit <k>` |
| `make app:log KIT=<k>` | `.venv\Scripts\python.exe scripts/app_host.py log --kit <k>` |
| `make eol.fix` / `eol.check` | `.venv\Scripts\python.exe scripts/normalize_eol.py` (add `--check`) |
| `make format.check` | `.venv\Scripts\python.exe scripts/clang_format.py --check` |

If `.venv` does not exist, say so and ask the user to run `make install`. Do not
pip-install packages into the system Python.

## Scope and boundary checks

```powershell
git diff --stat <base> -- poc-v1 docs/product
git diff <base> -- client/device/sdkconfig.defaults client/device/main/product_network.cpp
git diff <base> -- docs/plans | Select-String '^\+.*Status:'
git diff --stat <base>
```

Expected:

- The first three print nothing, unless the owner approved that exact change.
- `--stat` lists only allowed files. For any other file, revert it or explain
  why it changed.

## Tick and blocking checks (G2)

```powershell
git grep -nE "\b(fopen|fread|fwrite|fseek|fclose|mkdir)\(|std::remove\(" -- "client/device/main/*_service.cpp" "client/device/main/*_view_controller.cpp" client/device/main/product_runtime.cpp
git grep -n "portMAX_DELAY" -- "client/device/main/*_service.cpp" "client/device/main/*_view_controller.cpp" client/device/main/product_runtime.cpp
git grep -nE "esp_codec_dev_|bsp_audio_" -- "client/device/main/*_service.cpp" "client/device/main/*_view_controller.cpp" client/device/main/product_runtime.cpp
```

Expected: nothing. Each hit is either deleted or justified in the report as
provably not reachable from the LVGL task, with the caller chain.

## Memory checks (G3)

```powershell
git grep -nE "MALLOC_CAP_INTERNAL|std::malloc\(|\bmalloc\(" -- client/device/main
git grep -nE "static [A-Za-z_:<> ]+ [a-z_]+\[[0-9]{4,}\]" -- client/device/main
```

Expected: each hit added since `<base>` is listed in the report's memory notes
with size, region, and lifetime. A PSRAM request followed by an internal retry
is a defect.

## Worker and hardware-owner checks (G4)

```powershell
git grep -n "xTaskCreate" <base> -- client/device/main
git grep -n "xTaskCreate" -- client/device/main
git grep -n "bsp_audio_codec_speaker_init" -- client/device/main
git grep -n "vTaskDelay(1)" -- client/device/main
```

Expected:

- The second list has no task that is missing from the first, unless the plan
  allowed it.
- `bsp_audio_codec_speaker_init`: exactly one call site.
- `vTaskDelay(1)`: nothing new.

## Debug instrumentation removed

```powershell
git grep -n "DBG H" -- client
```

Expected: nothing. Debug logging from a `debug-device` session must be removed
before the work is reported as done.

## Build, test, line endings

```powershell
make test
make app:test
make app:build
make eol.check
```

## Device evidence (when a kit is attached)

For memory or connection work, compare the baseline and your build:

1. Build and flash `<base>` with `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC=y`, then
   capture a serial log (`make app:log`) through login and Wi-Fi association.
2. Repeat for your build.
3. From the `V2_MEM phase=ui_ready` line and the first line after Wi-Fi
   association, report internal free, internal largest block, DMA largest
   block, and PSRAM free for both.
4. Revert the diagnostic flag.

Flash evidence alone is **flash** class. A log containing
`product_ui_ready build=<id>` is **boot-marker**. Only a journey the operator
ran, with a saved log, is **behavior**.

## Report template

```text
Scope: <package / finding / bug>
Baseline commit: <sha>
Files changed: <list>
Files that looked relevant but were not changed: <list + why>
Root cause addressed: <one sentence, with the evidence>
Tests added: <names; each fails before and passes after>
Self-checks:
  <command>
  <pasted output>
  ...
Memory notes: <each buffer > 1 KiB added or removed: size, region, lifetime>
Tasks and hardware owners: <created/removed, stack size, region, priority>
Claim class reached: tooling | compile | flash | boot-marker | behavior
Evidence path: <log or build-report path, or "none">
Open questions / stop-and-ask: <none | list>
```
