#!/usr/bin/env bash
# Cross-platform Python paths for root Makefile recipes (Git Bash on Windows, bash on macOS).
#
# Usage:
#   scripts/make/python.sh host              # print bootstrap interpreter (python3 | python)
#   scripts/make/python.sh venv              # print repo .venv python if present, else host
#   scripts/make/python.sh run <args...>      # cd repo root; exec .venv python (else host)
#   scripts/make/python.sh host-run <args...> # cd repo root; exec host python (bootstrap)
#
# From the root Makefile:
#   $(GITBASH) "$(ROOT)/scripts/make/python.sh" host-run scripts/install.py
#
# Source from other scripts/make/*.sh (low-level; no automatic cd):
#   # shellcheck source=scripts/make/python.sh
#   source "$(dirname "${BASH_SOURCE[0]}")/python.sh"

set -euo pipefail

make_repo_root() {
  (cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
}

make_host_python() {
  if command -v python3 >/dev/null 2>&1; then
    command -v python3
    return 0
  fi
  if command -v python >/dev/null 2>&1; then
    command -v python
    return 0
  fi
  echo "python3 or python is required on PATH" >&2
  return 1
}

make_venv_python() {
  local root
  root="$(make_repo_root)"
  if [[ -x "$root/.venv/Scripts/python.exe" ]]; then
    echo "$root/.venv/Scripts/python.exe"
    return 0
  fi
  if [[ -x "$root/.venv/bin/python" ]]; then
    echo "$root/.venv/bin/python"
    return 0
  fi
  if [[ -x "$root/.venv/bin/python3" ]]; then
    echo "$root/.venv/bin/python3"
    return 0
  fi
  make_host_python
}

_make_python_main() {
  local cmd="${1:-}"
  shift || true
  case "$cmd" in
    host)
      make_host_python
      ;;
    venv)
      make_venv_python
      ;;
    run)
      cd "$(make_repo_root)"
      exec "$(make_venv_python)" "$@"
      ;;
    host-run)
      cd "$(make_repo_root)"
      exec "$(make_host_python)" "$@"
      ;;
    '')
      echo "usage: scripts/make/python.sh host|venv|run|host-run ..." >&2
      return 2
      ;;
    *)
      echo "unknown command: $cmd" >&2
      return 2
      ;;
  esac
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  _make_python_main "$@"
fi
