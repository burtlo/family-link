# LAB-07 readiness, roster, mute, and endpoint health

Make name: `lab07`. Variants: `manual` (default) and `scripted`. Build directories: `build/labs/lab-07-readiness-roster-manual` and `build/labs/lab-07-readiness-roster-scripted`.

```text
LAB-07/v1 = firmware lab07-fw-v1-manual / lab07-fw-v1-scripted + server lab07-server/v1 + fixture lab07-fixture/v1
```

stage_owner=lab07-local

One session writer in `components/lab07_session` applies stage transitions. Roster and health pollers only enqueue. Health results do not change the user stage. Stage tokens live in `components/lab07_session/include/stages.hpp`: `wifi_unavailable`, `connecting`, `roster`, `empty_hangout`, `muted`.

Boot marker:

```text
LAB07_READY build=<id> mode=<manual|scripted> fixture=lab07-health-roster/v1 harness=<0|1>
```

Scripted success, only after the harness finishes: `LAB07_HARNESS_DONE status=complete`. Display or required init failure prints `LAB07_INIT_FAILED` and does not print `LAB07_READY` or `LAB07_HARNESS_DONE status=complete`.

Trace prefix `LAB07_TRACE` with monotonic time, `session_id`, `user_id` where relevant, `operation_id`, and `generation`. No PIN, credential, URL secret, or raw media. There is no `wrong_pin` field.

Fixture schema `lab07-health-roster/v1`. Port 8767. Synthetic users `su-01` through `su-08` only.

Roster result queue capacity is 4, a provisional lab probe. Overflow emits one terminal, preserves the count, does not silently drop, and does not clear focus.

The 10-minute idle and 5-minute cadence are L1 virtual-clock only unless a run actually waits. Do not sleep 10 minutes on device or in host tests. Scripted mode runs the decided scenarios without taps, including one real 1-second request timeout, one heartbeat, and one roster refresh. Manual controls that change session state print each new terminal once as a `LAB07_TRACE` line when the control is handled. The hold prints its terminal when the real 1-second device timeout completes. Idle clock and stale beats advance the lab virtual clock inside the capture window. They are lab controls, not a real 10-minute wait and not product copy approval. Skipping a control leaves the experiment INCOMPLETE. `R-wire` and `R-stale-clock` stay exploratory and are not decided inputs. `CONFIG_LV_MEM_SIZE=65536` is the compile default, not a threshold. Do not declare a performance PASS.

ResourceBudget
  max_live_nodes: 32
  max_transient_bytes: 16384
  max_persistent_bytes: 4096
  max_concurrent_operations: 2
  max_stack_or_call_depth: 12
  maximum_input_size: 2048

These ResourceBudget numbers are a provisional lab probe. Firmware traces record two ledgers, LVGL pool and pixel/transient buffers, with high-water and largest free block fields.

## Manual screen

320x240. There is no instruction panel. The status line shows stage, user count, focused user id, stale, in-flight count, and whether a touch was seen during a hold. The stage under it shows the hangout. The rest of the screen is one control panel.

That panel has `Tap each once` at the top and one large button under it. The button names the current step, starting at `1/16  Wi-Fi down`. Tap it once and wait until the name advances. A second tap in that moment does not count and does not skip ahead. Hold 1 second means press that large button and keep your finger down. The wait starts when the finger goes down, and that contact counts. The status line changes to `touch=1` while the finger is still down. Letting go after that is enough. Holding the button does not skip ahead.

- Connecting: animated waiting and no cards. Copy is exploratory (`SCR-CONNECTING` remains OPEN).
- Wi-Fi unavailable: a distinct message and no cards. Copy is exploratory (`SCR-WIFI-UNAVAILABLE` remains OPEN).
- Roster: left-justified `Sign in as ...` and a horizontal carousel. All eight synthetic cards are addressable by scrolling.
- Empty hangout: a clear empty message, not the connecting animation and not a crash. Exact empty copy is exploratory.
- Muted: the display reads as off. A large `Unmute (lab stand-in)` control fills the middle of the black screen. It is a lab stand-in, not product chrome. Unmute starts an immediate readiness check. Tap it once.

Controls and expected visible results:

| Control | Expected visible result |
|---|---|
| Wi-Fi down | Wi-Fi unavailable message, no cards |
| Wi-Fi return | Immediate readiness check, then the synthetic roster |
| Empty roster | Empty hangout message |
| One user | One touchable card under Sign in as ... |
| Eight users | Horizontal carousel; scroll to reach all eight |
| Add | New card at the end, same focused id |
| Remove | That card is gone, focused id stays when that person remains |
| Reorder | Same focused id on a moved card |
| One failed check | Roster stays, then the refresh returns |
| Two failed checks | Connecting animation, no cards |
| Invalid HTTP 200 | Connecting, no cards |
| Hold 1 second | Screen stays touchable during the one-second hold, then the wait ends. A touch on a roster card during the wait sets touch |
| Mute | Display goes blank except the lab stand-in |
| Unmute stand-in | Immediate readiness check |
| Idle clock | Lab control. Advances the lab virtual clock through 600 s of quiet. Stage roster, roster period 300000 ms, heartbeat period 60000 ms, stale 0. Not a real 10-minute wait and not product copy approval |
| Stale beats | Lab control. Advances the lab virtual clock through three missed heartbeats. Stage roster unchanged, roster period 60000 ms, heartbeat period 60000 ms, stale 1. Not a real 10-minute wait and not product copy approval |

Native model checks compile the production C++ with `c++ -std=c++14` (or `g++` / `clang++` when that is the compiler on PATH). Host discovery runs `scripts/tests/test_lab07_model.py` and `scripts/tests/test_lab07_host.py`.
