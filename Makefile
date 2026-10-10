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

# Optional host INI: make config.host CONFIG=path/to.ini
CONFIG ?=
ifdef CONFIG
export PROJECT_CONFIG := $(CONFIG)
endif

MAKE_PY_SH := $(ROOT)/scripts/make/python.sh
_CONFIG_PY_ARGS := $(if $(CONFIG),--config "$(CONFIG)",)

# Python scripts (repo root + interpreter selection live in scripts/make/python.sh).
define py_host_run
	@$(GITBASH) "$(MAKE_PY_SH)" host-run $(1)
endef

define py_run
	@$(GITBASH) "$(MAKE_PY_SH)" run $(1)
endef

.PHONY: help config config.host config.deployment install test build devices flash flash.all device.bind

.DEFAULT_GOAL := help

help:
	@$(GITBASH) "$(ROOT)/scripts/make/help.sh"

config: config.host

config.host:
	$(call py_host_run,scripts/project_config.py $(_CONFIG_PY_ARGS) dump)

config.deployment:
	$(call py_run,scripts/deployment_config.py $(_CONFIG_PY_ARGS) dump)

install:
	$(call step_msg,Installing venv host tools and ESP-IDF if needed)
	$(call py_host_run,scripts/install.py)

test:
	$(call step_msg,Testing project configuration)
	$(call py_run,-m unittest discover -s scripts/tests)

build:
	$(call py_run,scripts/firmware.py build)

devices:
	$(call py_run,scripts/firmware.py devices)

flash:
	$(call py_run,scripts/firmware.py flash)

flash.all:
	$(call py_run,scripts/firmware.py flash --all)

flash.%:
	$(call py_run,scripts/firmware.py flash --kit "$*")

device.bind:
	$(call py_run,scripts/firmware.py bind --kit "$(KIT)" --port "$(PORT)")
