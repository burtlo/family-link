# LAB-05 transition proof and peak model

| Identity | Lifecycle (unchanged) | Peak (additive) |
|---|---|---|
| Product `lab_id` | `LAB-05` | `LAB-05` |
| Revision | `v1-proof` | `peak.lab_revision` `v1-peak` |
| Evidence slug | `lab-05` | `lab-05-peak` |
| Firmware project | `client/labs/lab-05-transition-proof` | same directory |
| Server | `none` | `none` |
| Trace schema | `lab05-transition/v1` | `lab05-peak-trace/v1` |
| Make name | `lab05` (already registered) | `lab05peak` is not registered in this package |

The lifecycle proof still loads two screens and prints `LAB05_READY`. Its harness markers remain `LAB05_HARNESS_BEGIN` and `LAB05_HARNESS_DONE status=transitioned`. Those strings stay in the `#else` branch of `main/app_main.c`. Peak boot and completion strings are only in the device harness, which CMake compiles when `CONFIG_LAB05_PEAK=y`. This package does not rename the project directory. `lab05peak` is not registered.

`CONFIG_LAB05_PEAK` defaults to off. `components/lab05_peak/CMakeLists.txt` adds `lab05_peak_model.c` and `lab05_peak_harness.c` only when that symbol is set. With the default, the component contributes no peak C file to the lifecycle link. The model sources are also wrapped in `#if CONFIG_LAB05_PEAK`. Kconfig `depends on` is not what keeps those files out of the lifecycle link.

## Model boundaries and ownership

`components/lab05_peak/lab05_peak_model.c` is the production domain model. Native tests compile that file with the host C compiler. There is no second test-only algorithm, no socket, and no call into another lab.

The session component is the only writer of stage and animation. Audio-stop and page-apply are local synthetic workers. They post results. A worker result cannot replace the stage or clear partial animation input.

Canvas is 320×240. One logical sketch image is a fixed 120×120 RGB565 buffer, 28,800 bytes, filled by the harness from the in-tree pattern. Two images are 57,600 bytes of pixel payload. That size is arithmetic, not a chosen wire format and not a pass ceiling.

Cover-down under test is 400 ms, with the incoming root moving from `y=+240` to `y=0`. Those parameters describe the animation under test. They are not a frame budget and they do not approve the persistent root or the visual.

### Operation envelope

An accepted operation has one id and exactly one terminal: `success`, `failure`, `cancelled`, `stale`, or `timed_out`. A second terminal for the same id is rejected and `terminal_count` stays 1. Repetitions use distinct ids. No scenario in this model waits on a network, so none of these scenarios ends as `timed_out`; the outcome exists so a later firmware wait cannot hide a timeout.

Cancellation is a session transition. It does not block the caller. Workers that already accepted a command still reach their own terminal.

### Queues

| Queue | Capacity | Overflow |
|---|---|---|
| audio-stop queue capacity | 8 | explicit rejection, count preserved, `dropped_count` stays 0, one `failure` / `overflow` terminal for the first rejected id |
| page-apply queue capacity | 8 | same rule, separate counter and terminal |

### ResourceBudget

All numbers below are a **provisional lab probe**, not a product budget and not a measured hardware envelope.

```text
ResourceBudget
  max_live_nodes: 20
  max_transient_bytes: 28800
  max_persistent_bytes: 28800
  max_concurrent_operations: 3
  max_stack_or_call_depth: 6
  maximum_input_size: 57600
  steady_live_images: 1
```

`steady_live_images` is the integer live-image count after cancel. It is a provisional lab probe, not a product budget. The persistent root keeps one image. The incoming image is released on cancel.

The probe arithmetic is: 16 summary nodes + 2 image nodes + 2 roots = 20 live nodes; the incoming image is the 28,800-byte transient buffer; the steady image is the 28,800-byte persistent buffer; one cover-down plus one running audio-stop and one running page-apply = 3 concurrent operations; the call-depth guard refuses entry past 6; two RGB565 images are the 57,600-byte pixel input cap.

### Update levels

Page apply is a Level 1 subtree update of 16 summary records. The identity key is `summary_id`. Applying the same page again does not allocate a second set of nodes. Data arrival does not destroy the shell.

Cover-down and replace are Level 2 and run only through the session writer. Handles carry a generation. Delete frees the pixel buffer only after every canvas view that references it has been released. A retained view defers that free. If the display lock fails, the scenario fails and the buffer stays allocated until teardown deletes the canvas and releases the view. Delete then clears the pointer and bumps the generation before any queued callback is delivered. A callback whose generation does not match does not read or write the freed pixels and ends as `stale`.

### Wait map

| Wait | Bound | Owner | On expiry |
|---|---|---|---|
| Cover-down | 400 ms animation length under test, not a pass limit | session writer | complete, or cancel while elapsed is below 400 ms |
| audio-stop | local command, no remote wait | audio-stop worker | its own terminal |
| page-apply | one 16-record page, no remote wait | page-apply worker | its own terminal |
| queue submit | non-blocking | caller | explicit overflow |

### Scenarios

`P-overlap` and `P-replace` are exploratory. They stay out of `experiment_inputs`. Decided inputs are `P-alloc-fail`, `P-stale-callback`, and `P-cancel`. This package does not write `experiment_verdict`. That check belongs to a later host package.

| Scenario | Class | Expected terminal |
|---|---|---|
| `P-overlap` | exploratory | one `success` for each of 20 distinct repetition ids; both images live at that terminal; missing, duplicate, or extra repetition terminals fail completeness |
| `P-replace` | exploratory | old tree freed before the incoming allocation; both a `success` and a `failure` / `alloc` are recorded on distinct ids; the control is not required to beat the persistent arm |
| `P-alloc-fail` | decided | injected allocation failure on the second image after the old handle is deleted; one `failure` / `alloc`; no read or write through the old handle |
| `P-stale-callback` | decided | callback queued before delete and delivered after; `stale`; zero writes through the old handle |
| `P-cancel` | decided | cancel during the 400 ms animation; `cancelled`, `terminal_count` 1, `live_image_count` equal to `steady_live_images`; workers reach their own terminals |

## Device harness

`app_main` calls `lab05_peak_harness_main` only from the `#if CONFIG_LAB05_PEAK` branch. The lifecycle two-screen path is the `#else` branch.

The harness drives the production model. It does not replace that model with a second algorithm. Audio-stop and page-apply are local FreeRTOS workers. Each queue capacity is 8. A full queue rejects the new command, increments `overflow_count`, leaves `dropped_count` at 0, and prints `LAB05_PEAK_PUBLISH`. The page worker posts a result. The scenario task applies the 16 summary labels while it holds the display lock. `page_worker` does not call `lv_label_set_text`. A display-lock failure requeues that result or prints `LAB05_PEAK_PUBLISH` with `status=rejected`. An accepted command is not dropped silently. Workers do not open a socket, and they do not apply a session transition.

Cover-down under test moves the incoming root from `y=+240` to `y=0` over 400 ms. Cancel stops that motion at the model's half-elapsed position. The 400 ms length is the animation under test, not a frame-budget pass limit. A 2000 ms wait is only a hang bound if the LVGL tick never completes the animation.

### Variants

| Variant | Build directory | `SDKCONFIG_DEFAULTS` (semicolon-separated, peak files last) |
|---|---|---|
| manual lifecycle | `build/labs/lab-05-transition-proof-manual` | `sdkconfig.defaults` |
| scripted lifecycle | `build/labs/lab-05-transition-proof-scripted` | `sdkconfig.defaults;sdkconfig.defaults.scripted` |
| peak manual | `build/labs/lab-05-transition-proof-peak-manual` | `sdkconfig.defaults;sdkconfig.defaults.peak` |
| peak scripted | `build/labs/lab-05-transition-proof-peak-scripted` | `sdkconfig.defaults;sdkconfig.defaults.scripted;sdkconfig.defaults.peak` |
| peak replace | `build/labs/lab-05-transition-proof-peak-replace` | `sdkconfig.defaults;sdkconfig.defaults.peak;sdkconfig.defaults.peak.replace` |
| peak scripted-replace | `build/labs/lab-05-transition-proof-peak-scripted-replace` | `sdkconfig.defaults;sdkconfig.defaults.scripted;sdkconfig.defaults.peak;sdkconfig.defaults.peak.replace` |

`sdkconfig.defaults.peak` sets `CONFIG_LAB05_PEAK=y` and `CONFIG_LAB05_BUILD_ID="lab05-peak-v1"`. `sdkconfig.defaults.peak.replace` sets `CONFIG_LAB05_PEAK_REPLACE=y` and `CONFIG_LAB05_BUILD_ID="lab05-peak-replace-v1"`. Neither lifecycle defaults file sets `CONFIG_LAB05_PEAK`. These overlays do not set `CONFIG_LV_MEM_SIZE`. The LVGL pool stays on the existing default. 65536 is not a pass threshold.

Generated sdkconfig expectations:

| Build | `CONFIG_LAB05_PEAK` | `CONFIG_LAB05_SCRIPTED` | `CONFIG_LAB05_PEAK_REPLACE` |
|---|---|---|---|
| manual lifecycle | not `y` | not `y` | not `y` |
| scripted lifecycle | not `y` | `y` | not `y` |
| peak manual | `y` | not `y` | not `y` |
| peak scripted | `y` | `y` | not `y` |
| peak replace | `y` | not `y` | `y` |
| peak scripted-replace | `y` | `y` | `y` |

Peak scripted is the scripted variant, not scripted-replace.

### Markers

Success boot line, printed only after display and UI init:

```text
LAB05_PEAK_READY build=<id> mode=<manual|scripted> control=<persistent|replace> server=none
```

`control=replace` follows `CONFIG_LAB05_PEAK_REPLACE`. Every other peak build uses `control=persistent`.

On display start failure the line is the same prefix with `status=display_start_failed`, and the success line is not printed. Display lock failure uses `status=display_lock_failed`. Any other required init failure uses `status=init_failed`.

Scripted success, after cleanup:

```text
LAB05_PEAK_HARNESS_DONE status=complete control=<persistent|replace>
```

Scripted failure prints `status=failed` and does not print `status=complete`. Manual mode does not print `LAB05_PEAK_HARNESS_DONE`.

### What each variant runs

Scripted persistent runs `P-overlap` for 20 repetitions, then the decided scenarios `P-alloc-fail`, `P-stale-callback`, and `P-cancel`. Each repetition has its own `operation_id`. After a scenario frees the steady image, the harness allocates one steady image again so the next scenario still has the model's steady state.

Scripted-replace runs the replace control for 20 repetitions, each with its own `operation_id`, without injecting allocation failure. The manual replace build has a separate `R-alloc` control that runs one injected `P-replace` allocation failure. The decided `P-alloc-fail` scenario belongs to the persistent harness, and its trace `control` is `persistent`. The replace arm is an exploratory control. A replace log that measured no decided scenario is `UNMEASURED`, not `FAIL`.

Manual persistent buttons run one overlap, alloc-fail, stale callback, or cancel. Manual replace buttons run one replace success or one injected replace allocation failure. `P-overlap` and `P-replace` stay exploratory. This firmware does not write `experiment_verdict`.

### Ledgers and trace fields

Two ledgers are printed at the live-image peak. Neither ledger is compared with a pass ceiling.

* `LAB05_PEAK_LEDGER ledger=lvgl_pool` reads `lv_mem_monitor` (`total_bytes`, `high_water_bytes`, `largest_free_bytes`) while the display lock is held. If that lock fails, those three fields are `unavailable`.
* `LAB05_PEAK_LEDGER ledger=pixel_transient` records `live_rgb565_count` and `temp_image_bytes` from the model. Two live RGB565 images are 57600 bytes of pixel payload. That number does not include LVGL or draw-buffer overhead. One steady image is the persistent 28800 bytes. A second live image is the transient 28800 bytes.

`LAB05_PEAK_TRACE` rows carry `scenario`, `kind` (`repetition`, `worker`, or `other`), `control`, `terminal`, `operation_id`, `terminal_count`, `live_image_count`, `class`, `reason`, and `temp_image_bytes`. The decided verdict and the repetition count use repetition rows. Worker rows stay separate operation results. Rows observed at the peak also carry `lvgl_high_water_bytes`, `lvgl_largest_free_bytes`, `internal_largest_free_bytes`, `psram_largest_free_bytes`, and `anim_frame_us` samples (comma-separated microseconds, plus `anim_frame_us_count`). `metrics_provenance=instrumented` means the harness sampled those counters in the firmware process. It does not mean a hardware evidence run. Internal and PSRAM fields stay `unmeasured`, and provenance is not `instrumented`, unless that sample ran. Worker rows that are not the observed peak keep the device fields `unmeasured`.

`LAB05_PEAK_COUNTERS` records the live image count, pixel payload, publish accepts, publish overflows, duplicate terminal rejects, and the local audio-stop flag after the scripted run. Cleanup stops the two workers, deletes their queues, and deletes the incoming LVGL root. The steady screen remains.

### idf.py builds

Direct `idf.py` builds. `lab05peak` is not a make target. Do not flash from these commands.

The project `CMakeLists.txt` appends `-O1` to one ESP-IDF source, `esp_lcd/rgb/esp_lcd_panel_rgb.c`. The v5.4.2 xtensa GCC 14.2.0 hits an internal compiler error in that file at the default `-Og`. The lab sources are not switched to `-O1`. This does not change `CONFIG_LV_MEM_SIZE` and it is not a frame-budget pass.

The commands actually run for this package, with `SDKCONFIG_DEFAULTS` set to the semicolon-separated list for that variant:

ESP-IDF was `C:\Users\lynnv\esp\esp-idf`. `git describe --tags --exact-match` in that checkout was `v5.4.2`. `idf.py` was `C:\Users\lynnv\.espressif\python_env\idf5.4_py3.14_env\Scripts\python.EXE C:\Users\lynnv\esp\esp-idf\tools\idf.py`. Target `esp32s3`. Each command exited 0. `CONFIG_LV_MEM_SIZE` stayed `65536` in every generated sdkconfig. The peak overlays do not set it.

On the two lifecycle sdkconfigs, `CONFIG_LAB05_PEAK_REPLACE` does not appear. The option depends on `CONFIG_LAB05_PEAK`, which is not set, so the replace symbol is not `y`.

```text
SDKCONFIG_DEFAULTS=C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults
C:\Users\lynnv\.espressif\python_env\idf5.4_py3.14_env\Scripts\python.EXE C:\Users\lynnv\esp\esp-idf\tools\idf.py -C C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof -B C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-manual -DIDF_TARGET=esp32s3 -DSDKCONFIG=C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-manual\sdkconfig build
```

Result: `# CONFIG_LAB05_PEAK is not set`, `# CONFIG_LAB05_SCRIPTED is not set`, `CONFIG_LAB05_BUILD_ID="lab05-manual-v1"`. The elf contains `LAB05_READY` and does not contain `LAB05_PEAK_READY` or `LAB05_PEAK_HARNESS_DONE`.

```text
SDKCONFIG_DEFAULTS=C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults;C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults.scripted
C:\Users\lynnv\.espressif\python_env\idf5.4_py3.14_env\Scripts\python.EXE C:\Users\lynnv\esp\esp-idf\tools\idf.py -C C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof -B C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-scripted -DIDF_TARGET=esp32s3 -DSDKCONFIG=C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-scripted\sdkconfig build
```

Result: `# CONFIG_LAB05_PEAK is not set`, `CONFIG_LAB05_SCRIPTED=y`, `CONFIG_LAB05_BUILD_ID="lab05-scripted-v1"`. The elf contains `LAB05_READY` and does not contain the peak marker strings.

```text
SDKCONFIG_DEFAULTS=C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults;C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults.peak
C:\Users\lynnv\.espressif\python_env\idf5.4_py3.14_env\Scripts\python.EXE C:\Users\lynnv\esp\esp-idf\tools\idf.py -C C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof -B C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-peak-manual -DIDF_TARGET=esp32s3 -DSDKCONFIG=C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-peak-manual\sdkconfig build
```

Result: `CONFIG_LAB05_PEAK=y`, `# CONFIG_LAB05_SCRIPTED is not set`, `# CONFIG_LAB05_PEAK_REPLACE is not set`, `CONFIG_LAB05_BUILD_ID="lab05-peak-v1"`. The elf contains both peak marker strings and does not contain `LAB05_READY`.

```text
SDKCONFIG_DEFAULTS=C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults;C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults.scripted;C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults.peak
C:\Users\lynnv\.espressif\python_env\idf5.4_py3.14_env\Scripts\python.EXE C:\Users\lynnv\esp\esp-idf\tools\idf.py -C C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof -B C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-peak-scripted -DIDF_TARGET=esp32s3 -DSDKCONFIG=C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-peak-scripted\sdkconfig build
```

Result: `CONFIG_LAB05_PEAK=y`, `CONFIG_LAB05_SCRIPTED=y`, `# CONFIG_LAB05_PEAK_REPLACE is not set`, `CONFIG_LAB05_BUILD_ID="lab05-peak-v1"`. Both peak marker strings are in the elf. `LAB05_READY` is not.

```text
SDKCONFIG_DEFAULTS=C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults;C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults.peak;C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults.peak.replace
C:\Users\lynnv\.espressif\python_env\idf5.4_py3.14_env\Scripts\python.EXE C:\Users\lynnv\esp\esp-idf\tools\idf.py -C C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof -B C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-peak-replace -DIDF_TARGET=esp32s3 -DSDKCONFIG=C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-peak-replace\sdkconfig build
```

Result: `CONFIG_LAB05_PEAK=y`, `# CONFIG_LAB05_SCRIPTED is not set`, `CONFIG_LAB05_PEAK_REPLACE=y`, `CONFIG_LAB05_BUILD_ID="lab05-peak-replace-v1"`. Both peak marker strings are in the elf. `LAB05_READY` is not.

```text
SDKCONFIG_DEFAULTS=C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults;C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults.scripted;C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults.peak;C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof\sdkconfig.defaults.peak.replace
C:\Users\lynnv\.espressif\python_env\idf5.4_py3.14_env\Scripts\python.EXE C:\Users\lynnv\esp\esp-idf\tools\idf.py -C C:\Users\lynnv\src\family-link-lab05\client\labs\lab-05-transition-proof -B C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-peak-scripted-replace -DIDF_TARGET=esp32s3 -DSDKCONFIG=C:\Users\lynnv\src\family-link-lab05\build\labs\lab-05-transition-proof-peak-scripted-replace\sdkconfig build
```

Result: `CONFIG_LAB05_PEAK=y`, `CONFIG_LAB05_SCRIPTED=y`, `CONFIG_LAB05_PEAK_REPLACE=y`, `CONFIG_LAB05_BUILD_ID="lab05-peak-replace-v1"`. Both peak marker strings are in the elf. `LAB05_READY` is not.

## Fixtures

`fixtures/synthetic-page.json` is 16 synthetic summary records. `fixtures/image-a.rgb565` and `fixtures/image-b.rgb565` are 28,800-byte harness patterns (constant RGB565 `0x001F`, and a green-channel index mask). They are not photographs, household media, or a sketch wire format. `fixtures/manifest.json` records their lengths and hashes. `fixtures/resource-budget.json` is the versioned copy of the probe numbers.

## Trace contract

`peak-trace-schema.json` is schema `lab05-peak-trace/v1`. Rows use the prefix `LAB05_PEAK_TRACE`.

A later parser needs `scenario`, `control`, `terminal`, `operation_id`, `terminal_count`, and `live_image_count`. Model rows also carry `class`, `reason`, `temp_image_bytes`, and `kind`. `temp_image_bytes` here is the model's live RGB565 payload at the terminal sample (two images = 57600). It is accounting, not a device measurement, and it is not a pass ceiling.

These device metric names are part of the contract and are emitted as `unmeasured` by the host model: `lvgl_high_water_bytes`, `lvgl_largest_free_bytes`, `internal_largest_free_bytes`, `psram_largest_free_bytes`, `anim_frame_us`. The device harness samples the same names while the scenario is at its live-image peak and prints `metrics_provenance=instrumented`. That is not `hardware`. This package does not claim they were measured on a device, and it does not write an evidence run or `metrics.json`.

S-10 parser `redact_line` coverage is HOST-owned. This MODEL package does not claim S-10 is satisfied. Fixtures and model traces still contain no PIN, password, Bearer token, URL credential, or raw media payload.

## Native test

From the repository root:

```text
.venv\Scripts\python.exe -m unittest scripts.tests.test_lab05_peak_model
```

Root discovery loads the same module with `python -m unittest discover -s scripts/tests`. The launcher compiles `components/lab05_peak/lab05_peak_model.c` and `tests/test_lab05_peak_native.c` with the host C compiler and `-DCONFIG_LAB05_PEAK=1`, then runs the binary. Host results do not qualify a device build, a performance verdict, or a visual.
