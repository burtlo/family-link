#!/usr/bin/env bash
# Printed by `make` / `make help` (Git Bash on Windows; /bin/bash on macOS).
set -euo pipefail

echo "////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\"
echo
echo "  Family Link"
echo
echo "\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////"
echo
echo "  SETUP"
echo "    make install     Prepares the host: Python venv, serial/flash tools, ESP-IDF when absent."
echo
echo "\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////"
echo
echo "  CONFIGURATION"
echo "    Host defaults: config/host.defaults.ini"
echo "    Use another host INI: make config.host CONFIG=path/to.ini  (or export PROJECT_CONFIG)."
echo "    Override any value with an uppercase env var (e.g. IDF_SKIP=true)."
echo
echo "    make config.host        Host snapshot JSON (toolchain, commands, flows)."
echo "    make config.deployment  Product roster snapshot JSON (redacted)."
echo "    make config.labs        Device labs registry snapshot JSON."
echo "    make config             Same as make config.host."
echo
echo "\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////"
echo
echo "  TEST"
echo "    make test        Runs the active repository unit tests."
echo
echo "  DEVICE BOOTSTRAP"
echo "    make build         Build and package fresh v2 bootstrap firmware; no USB needed."
echo "    make devices       Show nicknames, USB bindings and detected ports."
echo "    make flash         Flash the existing package to one connected registered kit."
echo "    make flash.mazi    Select a kit nickname (also arlo, lynn, audrey)."
echo "    make flash.all     Preflight every roster kit, then flash sequentially."
echo "    make device.bind KIT=arlo PORT=COM7    Explicit one-time USB binding."
echo
echo "\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////"
echo
echo "  DEVICE LABS (ESP-IDF experiments under client/labs/)"
echo "    Registry: config/labs.registry.yaml"
echo "    make lab.help              List registered labs."
echo "    make lab.lab02.help        What to do on hardware (read this first)."
echo "    make lab.lab02             Manual run — you scroll the inbox during capture."
echo "    make lab.lab02.scripted    Auto-scroll — device runs the script (HARNESS_DONE)."
echo "    make lab.lab02.eager       Eager 16-card comparison image."
echo "    make lab.lab02.server      Run the paging server on port 8765 (Ctrl-C to stop)."
echo "    make lab.lab02.parse       Parse capture to metrics.json (latest or RUN_ID=)."
echo "    make lab.lab02.build       Build manual (recycle) image only."
echo "    Options: KIT=lynn PORT=COM7 SECONDS=120 FLASH=0 RUN_ID=..."
echo
echo
