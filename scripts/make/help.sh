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
echo "  ASSETS"
echo "    make avatars     Pack top-level images in docs/product/device-ui/avatars/"
echo "                     into client/assets/portraits/ (RGB565A8, updates avatars.json)."
echo
echo "  DEVICE BOOTSTRAP"
echo "    make build         Build and package fresh v2 bootstrap firmware; no USB needed."
echo "    make devices       Show nicknames, USB bindings and detected ports."
echo "    make flash         Flash the existing package to one connected registered kit."
echo "    make flash.<kit>   Flash a selected kit (for example, alpha or beta)."
echo "    make flash.all     Preflight every roster kit, then flash sequentially."
echo "    make device.bind KIT=<kit> PORT=<port>  Explicit one-time USB binding."
echo
echo "  PRODUCT SERVER"
echo "    make server        P1 core-journey wire fixture (0.0.0.0:8777; alias: make lab.p1.server)."
echo
echo "  PRODUCT APP (same firmware tree; lab-style kit selection on flash)"
echo "    make app           Build and flash (app:build then app:flash)."
echo "    make app:build     Build client/device and publish a firmware package."
echo "    make app:flash     Flash current package; auto-picks one kit or prompts if several."
echo "                       Optional: KIT=<nickname> PORT=<port>"
echo
echo "\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////"
echo
echo "  DEVICE LABS (experiments under client/labs/)"
echo
echo "    make config.labs        Device labs registry snapshot JSON."
echo "    make lab.help           List registered labs with descriptions."
echo
echo "    LABS"
echo
    ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
    bash "$ROOT/scripts/make/python.sh" run scripts/lab_host.py list
echo
echo "   ACTIONS"
echo "      [help, build, flash, capture, clean]"
echo "      server             Start a lab server in the foreground"
echo
echo "    OPTIONS"
echo "      KIT=<kit>          Optional kit override; auto-detects one connected kit"
echo "      PORT=<port>        Optional port override; follows the selected kit"
echo "      SECONDS=<seconds>  Capture duration"
echo "      FLASH=0|1          Skip or force flashing"
echo "      RUN_ID=<run-id>    Evidence run identifier"
echo "      BACKEND=<backend>  managed-http (default) or local comparison"
echo
echo "   USAGE"
echo "     make lab.<lab>.<action> [OPTIONS]  Run a lab action with optional overrides."
