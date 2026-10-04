# h30 Opus (host tools, phases 1–2)

No chunk server yet (phase 3). Use:

```bash
python scripts/opus_inspect.py --self-test
python scripts/opus_inspect.py demos/server/h30_opus/fixtures/beep_1s_16k_mono.wav
```

Fixture WAV is generated on first `--self-test` if missing. Full server round-trip lands in phase 3.
