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
echo "    make lab.help              List registered labs with descriptions."
echo "    make lab.<name>.help       Show operator instructions and core actions."
echo
echo "    Registered labs:"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT/scripts/make/python.sh" run scripts/lab_host.py list
echo
echo "    Core actions (available where supported):"
echo "      make lab.<name>                 Manual run"
echo "      make lab.<name>.scripted         Scripted run"
echo "      make lab.<name>.build            Build"
echo "      make lab.<name>.flash            Flash last built image"
echo "      make lab.<name>.capture          Capture current device state"
echo "      make lab.<name>.clean            Remove the default build"
echo "      make lab.<name>.scripted.build   Build scripted image"
echo "      make lab.<name>.scripted.flash   Flash scripted image"
echo "      make lab.<name>.scripted.capture Capture scripted results"
echo "      make lab.<name>.scripted.clean   Remove scripted build"
echo "      make lab.<name>.server           Start a lab server in foreground"
echo "      make lab.<name>.help             Show lab-specific instructions"
echo "    Options: KIT=lynn PORT=COM7 SECONDS=120 FLASH=0 RUN_ID=..."
echo
echo
