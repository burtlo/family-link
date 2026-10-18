# LAB-10 scripted canvas color diagnostic

- Run: `20261018-scripted-run-4afc6192`
- Kit: `lynn-kit`
- Workflow: `PASS`
- Experiment verdict: `UNMEASURED`
- Scenario: `scripted-human-like-multicolor-180s-v1`
- Capture: scripted 180-second duration cap
- Target build and flash: passed; flashed image hash verification passed
- Binary/source fingerprint: not present in the generated run manifest

## Device diagnostic

The terminal serial marker reports:

```text
LAB10_SCRIPTED_DIAGNOSTIC terminal=duration_cap capture_session_id=1 accepted_points=3450 strokes=18 palette_changes=17 accepted_color_mask=0x3e queue_overflow_count=0 trace_loss_count=0 duration_cap_count=1 terminal_event_count=1 completion_scheduler_accepted=1 renderer_published_color_mask=0x3e canvas_sampled_color_mask=0x3e canvas_readback_color_mask=0x3e canvas_color_match_mask=0x3e canvas_readback_supported=1 canvas_color_format=18 renderer_drain_complete=1
```

This confirms that all five ink-color indices were accepted, reached the
renderer publication callback, and each had a sampled LVGL canvas-buffer write
whose RGB565 readback matched the expected color. The renderer was drained
before the final diagnostic was emitted. Queue overflow and trace loss were
zero; the capture reached one duration-cap terminal event.

The per-color canvas sample is taken on the first published pixel of each color;
it is not a histogram of final post-overdraw canvas contents. It also does not
prove LCD scanout. After this run, the user reported that multiple colors were
visible on the screen. This is direct human observation for this run, not a
synchronized screen recording. The experiment verdict correctly remains
`UNMEASURED` because it covers the broader experiment, not merely color
visibility.

## Artifacts

- Serial log: `logs/device/labs/lab-10/20261018-scripted-run-4afc6192/serial.txt`
- Run manifest: `docs/labs/evidence/lab-10/runs/20261018-scripted-run-4afc6192/run-manifest.json`
- Parsed metrics: `docs/labs/evidence/lab-10/runs/20261018-scripted-run-4afc6192/metrics.json`
- Event journal: `docs/labs/evidence/lab-10/runs/20261018-scripted-run-4afc6192/events.jsonl`

## Disposition

For `LAB10-F011`, device diagnostics verify all five ink colors reached matching
RGB565 values in the LVGL canvas buffer, and the user's post-run observation
confirms multiple colors were visibly displayed. This resolves the reported
white-only symptom for the freshly flashed run. The earlier run's cause remains
unlocalized; its stale-image origin is not established. The latest final
post-overdraw color-mask instrumentation was added after this capture and was
not exercised on target. Its independent review passed the LAB-10 Python suite
(68 tests), manual and scripted ESP-IDF builds, and scoped diff check. The
one-shot 14,400-pixel scan runs in the 20 ms UI polling task; target latency is
unmeasured, so a brief completion-time UI pause remains possible. No
synchronized screen recording is available.
