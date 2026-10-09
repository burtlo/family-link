# Family Link — active repo host tasks (GNU Make + Python 3.9+).
# Windows: Git for Windows bash for recipes (see porcelain-style GITBASH below).
# macOS: /bin/bash. Daily shells: PowerShell 7 (Windows), zsh (macOS) — docs/AGENTS.md.

ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

ifeq ($(OS),Windows_NT)
  ifeq ($(origin GITBASH),undefined)
    _GIT_BASH := $(wildcard $(ProgramW6432)/Git/bin/bash.exe)
    ifeq ($(_GIT_BASH),)
      _GIT_BASH := $(wildcard $(LOCALAPPDATA)/Programs/Git/bin/bash.exe)
    endif
    ifneq ($(_GIT_BASH),)
      GITBASH := "$(firstword $(_GIT_BASH))"
    else
      GITBASH := "$(ProgramW6432)/Git/bin/bash.exe"
    endif
  endif
else
  ifeq ($(origin GITBASH),undefined)
    GITBASH := bash
  endif
  SHELL := /bin/bash
endif

define step_msg
	@$(GITBASH) -c "echo '[STEP] $(1)'"
endef

export PYTHONUNBUFFERED := 1

MAKE_PY_SH := $(ROOT)/scripts/make/python.sh

# Repo-root Python via scripts/make/python.sh (host-run = bootstrap; run = .venv if present).
define py_host_run
	@$(GITBASH) -c "cd '$(ROOT)' && '$(MAKE_PY_SH)' host-run $(1)"
endef

define py_run
	@$(GITBASH) -c "cd '$(ROOT)' && '$(MAKE_PY_SH)' run $(1)"
endef

.PHONY: help install

.DEFAULT_GOAL := help

help:
	@$(GITBASH) "$(ROOT)/scripts/make/help.sh"

install:
	$(call step_msg,Installing venv host tools and ESP-IDF if needed)
	$(call py_host_run,scripts/install.py)
