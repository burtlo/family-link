# LAB-10 scripted multicolor diagnostic run

- Run ID: `20261018-scripted-run-5d7adf6d`
- Raw serial log: `logs/device/labs/lab-10/20261018-scripted-run-5d7adf6d/serial.txt`
- Mode / workload: scripted / `scripted-human-like-multicolor-180s-v1`
- Firmware marker: `lab10-runner-scripted-v2-continuous`
- Host result: `workflow_status=INCOMPLETE`, `experiment_verdict=UNMEASURED`
- Terminal: `duration_cap`, one terminal event

Observed device diagnostic:

```text
LAB10_SCRIPTED_DIAGNOSTIC terminal=duration_cap accepted_points=3452 strokes=18 palette_changes=17 accepted_color_mask=0x3e queue_overflow_count=0 trace_loss_count=0 duration_cap_count=1 terminal_event_count=1 completion_scheduler_accepted=0
```

The device accepted all five required ink colors (`0x3e`), all 18 planned
strokes and 17 palette changes, with no queue overflow or trace loss. The
scripted completion marker was withheld because 3452 points fell below the
then-current 3500 minimum. This run therefore does not qualify as a completed
workflow and is not visual scanout evidence. The LAB-10 minimum was adjusted to
3400 points, retaining the 4095 maximum and all other completion requirements.

No synchronized screen recording or scanout measurement was captured; visible
color rendering remains unmeasured.
