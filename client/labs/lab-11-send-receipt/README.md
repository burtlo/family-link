# LAB-11 — send, receipt, and recovery

Make name: `lab11`. Variants: `manual` (`build/labs/lab-11-send-receipt-manual`) and `scripted` (`build/labs/lab-11-send-receipt-scripted`). Boot marker:

```text
LAB11_READY build=<id> mode=<manual|scripted> fixture=lab11-send/v1 harness=<0|1>
```

Scripted completion marker: `LAB11_HARNESS_DONE status=complete`. Display or required init failure prints `LAB11_INIT_FAILED status=display_start_failed` and does not print the ready marker.

Logical version:

```text
LAB-11/v1 = firmware lab11-fw-scripted-v1 + server lab11-server/v1 + fixture lab11-send/v1
```

The manual build id is `lab11-fw-manual-v1`. Server: `server/labs/lab-11-send-receipt` on port **8770**, bound on all interfaces for a device run. Trace: `lab11-trace/v1`.

A scripted device build with a selected kit writes a private overlay beside the build directory. That overlay sets the fixture host to this computer's LAN address and the kit Wi-Fi profile. The committed default host stays `127.0.0.1`. The harness joins that station before the first request and prints `LAB11_STATION status=joined` or `status=join_failed`. `LAB11_HARNESS_DONE status=complete` still requires the fixture exchanges to finish.

## Visible controls

The scene root is created once. Progress and phase text are Level 0 property updates (`lv_label_set_text` on the existing labels). Text sits in the 296 by 216 content well, inset 12 px from the 320 by 240 canvas. Labels use Montserrat 16. The linked face is LVGL's built-in Montserrat, not weight 600. Each button is 144 by 48 px, above the 101 by 48 minimum, in a two-by-two grid with an 8 px gap.

The screen shows fixture `lab11-send/v1`, the frozen capture hash, accepted point count, `duration_cap_ms` 180000, logical 120x120, capture queue capacity 16, the active acceptance condition `accept-on-archive-commit`, the expected result for the active scenario, chunk probe 1024 or 4096, and part queue capacity 8 as a provisional lab probe.

Buttons, all of which return without waiting for upload, lookup, or cleanup:

| Control | Action |
|---|---|
| Next | Selects the next scenario label and expected result. |
| Chunk | Switches the chunk probe between 1024 and 4096. |
| Run | Starts the send harness on a worker task. |
| Cancel | Sets the cancellation flag and returns. |

Success phase text is `condition met`, and only after the active condition is met. The product word Sent is not an outcome. `COM-OPEN-001` stays OPEN.

## Ownership

`lab11_capture` owns the immutable synthetic payload and the recipient list. A send operation borrows that object at begin and copies the hash, length, and recipient ids into the operation. The send path never frees the payload. It releases the borrow on success, failure, cancellation, and stale completion, and frees only its staging and response buffers. `lab11_capture_free` refuses while a borrow is held. Offline and storage-full state the disposition explicitly (`work=discarded`). Local cancel does not roll back a server accept.

## Queues and budget

These numbers are a **provisional lab probe**, not a product budget or a device measurement.

| ResourceBudget field | Provisional lab probe |
|---|---:|
| `max_live_nodes` | 32 |
| `max_transient_bytes` | 8704 |
| `max_persistent_bytes` | 5120 |
| `max_concurrent_operations` | 1 |
| `max_stack_or_call_depth` | 8 |
| `maximum_input_size` | 4096 |

Part queue capacity is **8**. A ninth admission is one `failure/overflow`, the count stays 8, and nothing is silently dropped. The worker publication path posts one parsed event at a time; the UI task applies terminals. Calculated peaks, not device heap:

| Chunk | Retained input | Staging | Transport copy | Retry extra | Response | Peak |
|---|---:|---:|---:|---:|---:|---:|
| 1024 | 5120 | 1024 | 0 on the host socket, 1024 if the HTTP client copies one chunk | 0 | 512 | 6656 host / 7680 with one chunk copy |
| 4096 | 5120 | 4096 | 0 host / 4096 one-chunk copy | 0 | 512 | 9728 host / 13824 with one chunk copy |

`full_payload_duplicate_bytes` stays 0. Staging is one window, refilled from the borrowed payload on retry. That is not a measurement of device heap, LVGL pool, or part timing. Those stay `UNMEASURED`.

## Checks

Native model tests compile the production C sources:

```text
gcc -std=c11 -Wall -Wextra -Werror -pedantic -I components/lab11_capture/include -I components/lab11_send/include components/lab11_capture/lab11_capture.c components/lab11_capture/lab11_sha256.c components/lab11_send/lab11_send.c tests/model_test.c -o lab11_model_test.exe -lws2_32
```

`scripts/tests/test_lab11_model.py` and `scripts/tests/test_lab11_host.py` are discovered by `make test`.
