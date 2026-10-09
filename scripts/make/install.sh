#!/usr/bin/env bash
# Invoked by root Makefile `install`.
set -euo pipefail
# shellcheck source=scripts/make/python.sh
source "$(dirname "$0")/python.sh"
cd "$(make_repo_root)"
exec "$(make_host_python)" scripts/install.py "$@"
