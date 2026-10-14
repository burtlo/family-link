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
echo "    make flash.<kit>   Flash a selected kit (for example, alpha or beta)."
echo "    make flash.all     Preflight every roster kit, then flash sequentially."
echo "    make device.bind KIT=<kit> PORT=<port>  Explicit one-time USB binding."
echo
echo "\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////"
echo
echo "  DEVICE LABS (ESP-IDF experiments under client/labs/)"
echo "    Registry: config/labs.registry.yaml"
echo "    make lab.help              List registered labs with descriptions."
echo
echo "    Registered labs:"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT/scripts/make/python.sh" run scripts/lab_host.py list
echo
echo "    Actions (available where supported):"
echo "      <default>          Manual run"
echo "      scripted           Scripted run"
echo "      build              Build"
echo "      flash              Flash the last built image"
echo "      capture            Capture current device state"
echo "      clean              Remove the default build"
echo "      scripted.build     Build scripted image"
echo "      scripted.flash     Flash scripted image"
echo "      scripted.capture   Capture scripted results"
echo "      scripted.clean     Remove scripted build"
echo "      server             Start a lab server in the foreground"
echo "      help               Show lab-specific instructions"
echo "      Command form: make lab.<name>[.<action>]"
echo
echo "    Options:"
echo "      KIT=<kit>          Deployment kit nickname"
echo "      PORT=<port>        Explicit serial port"
echo "      SECONDS=<seconds>  Capture duration"
echo "      FLASH=0|1          Skip or force flashing"
echo "      RUN_ID=<run-id>    Evidence run identifier"
echo "      BACKEND=<backend>  managed-http (default) or local comparison"
echo
echo
