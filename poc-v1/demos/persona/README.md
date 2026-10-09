# Parent likeness pipeline (host). Plan: ../../docs/PERSONA-ASSETS.md

Stand-ins by default — no photo or voice required.

```
make install-persona
make demos-persona          # a01–a05, writes data/persona/ + example C headers
make flash DEMO=p10         # portrait on the box
make flash DEMO=p11         # greeting on mute tap
```

Your media later (stays out of git):

```
python demos/persona/01_capture/capture.py --in ~/Photos/desk.jpg
python demos/persona/02_cut/cut.py --box 80,40,220,220
python demos/persona/03_record/record.py --mic --seconds 2
python demos/persona/04_ideas/ideas.py
python demos/persona/05_pack/pack.py --dest firmware/assets/persona
```

`--webcam` / `--mic` need ffmpeg. Packed overlay dir `firmware/assets/persona/` is gitignored.
