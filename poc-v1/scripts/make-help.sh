#!/usr/bin/env bash
# Printed by `make help` (single bash process; avoids printf/echo quirks across shells).
set -euo pipefail

echo "////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\"
echo
echo "  Family desk link — ESP32-S3-BOX-3 host tasks"
echo
echo "\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////"
echo
echo "            +------------------+------------------------------------------+"
echo "  SETUP:    | install          | Python venv + esptool (PyPI)             |"
echo "            | install-server   | + FastAPI/httpx for protocol demos       |"
echo "            | install-persona    | + Pillow for parent photo/voice packing  |"
echo "            +------------------+------------------------------------------+"
echo
echo "  make install | install-server | install-persona"
echo
echo "----/////----/////----/////----/////----/////----/////----/////----/////---/////---"
echo
echo "  INSPECT (USB, no ESP-IDF):"
echo
echo "  make connect         USB detect → port → ROM sync → chip"
echo "  make system          OS, firmware versions, host/device environment"
echo "  make storage         flash size, partitions, filesystems"
echo "  make report          connect, then system, then storage"
echo
echo "----/////----/////----/////----/////----/////----/////----/////----/////---/////---"
echo
echo "  FLASH (ESP-IDF; USB-C on the box, not the dock):"
echo
echo "  make idf-install              clone ESP-IDF to ~/esp/esp-idf + esp32s3 tools"
echo "  make flash DEMO=h02           build + flash one firmware demo"
echo "  make flash-monitor DEMO=h02   flash then serial monitor"
echo "  make build-firmware DEMO=h02  compile only"
echo "  make monitor                  serial monitor (115200 / idf.py)"
echo "  make flash-list               which demo .c files exist"
echo
echo "  make h01 … h29    firmware demo aliases (h20–h22 / h26 / h27 = two-box; h28 = LCD clip)"
echo "  make x01          legacy product shell (peer inbox)"
echo "  make p01 … p13    personality / face / packed portrait"
echo
echo "----/////----/////----/////----/////----/////----/////----/////----/////---/////---"
echo
echo "  V1 DEVICE (carousel firmware on the box):"
echo
echo "  make x02 | flash DEMO=x02   build + flash v1 shell (hangout users, PIN, inbox)"
echo "  make v1-timing              timing.yaml → firmware/v1 + web twin headers/JS"
echo "  make check-v1-parity        timing.yaml vs firmware vs demos/server/v1_product/web"
echo "  WHO=mazi | arlo             kit identity when flashing (see make flash overrides)"
echo "  PROFILE=sunset | anamcara   Wi-Fi profile (default sunset); override or kits.local.yaml"
echo
echo "----/////----/////----/////----/////----/////----/////----/////----/////---/////---"
echo
echo "  V1 SERVER (product host + admin + web twin):"
echo
echo "  make v1-server              FastAPI host on :8080"
echo "  make v1-server-tls          same on :8443 (data/certs/dev*.pem)"
echo "  make demo-v1                smoke test (hangout users + inbox)"
echo "  open http://localhost:8080/box/     320×240 web twin (firmware/v1 parity)"
echo
echo "----/////----/////----/////----/////----/////----/////----/////----/////---/////---"
echo
echo "  SERVER PROTOCOL DEMOS (no box required):"
echo
echo "  make demo-auth | demo-heartbeat | demo-messages | demo-playback"
echo "  make demo-cursor | demo-hangout | demo-relay | demo-presence | demo-talk"
echo "  make demo-diary | demo-device-log | demo-draw | demo-sketch | demo-pingpong"
echo "  make demo-combined | demos-server"
echo
echo "  Web twin: python demos/server/h18_playback/server.py --host 0.0.0.0 --port 8080"
echo "            open http://localhost:8080/box/"
echo
echo "----/////----/////----/////----/////----/////----/////----/////----/////---/////---"
echo
echo "  PARENT LIKENESS (stand-ins until you pass --in):"
echo
echo "  make demo-capture | demo-cut | demo-record | demo-ideas | demo-pack"
echo "  make demos-persona              run a01–a05"
echo "  make flash DEMO=p10 | p11       packed portrait / greeting"
echo
echo "----/////----/////----/////----/////----/////----/////----/////----/////---/////---"
echo
echo "  OVERRIDES:"
echo "  PORT=/dev/cu.usbmodem…   first bind only if two kits"
echo "  DEMO=x02 WHO=mazi        then WHO=arlo — one compile if same PROFILE; serial remembered"
echo "  DEMO=x02 WHO=arlo PROFILE=anamcara   other Wi-Fi (default PROFILE=sunset)"
echo "  Cable: USB-C on the box, not the dock."
echo
echo "  Parent Mac (phone stand-in): demos/parent/*.py — see docs/DEMO-MAP.md"
echo "  TLS: python scripts/dev_https.py — docs/TLS.md"
echo
