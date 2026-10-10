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
echo "    make config             Same as make config.host."
echo
echo "\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////\\\\\\\\////"
echo
echo "  TEST"
echo "    make test        Runs the active repository unit tests."
echo
echo
