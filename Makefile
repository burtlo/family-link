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

.PHONY: help config config.host config.deployment config.labs install test build devices flash flash.all device.bind lab.help

# Lab device firmware (registry: config/labs.registry.yaml). Optional: KIT PORT SECONDS FLASH RUN_ID
# Prefer make lab.<name>.scripted / .eager over VARIANT= (advanced).
.PHONY: lab.%.build lab.%.flash lab.%.capture lab.%.clean lab.%.help
.PHONY: lab.%.scripted lab.%.scripted.build lab.%.scripted.flash lab.%.scripted.capture lab.%.scripted.clean
.PHONY: lab.%.phaseb lab.%.phaseb.build lab.%.phaseb.flash lab.%.phaseb.capture lab.%.phaseb.clean
.PHONY: lab.%.eager lab.%.eager.build lab.%.eager.flash lab.%.eager.capture lab.%.eager.clean
.PHONY: lab.%.parse

.DEFAULT_GOAL := help

help:
	@$(GITBASH) "$(ROOT)/scripts/make/help.sh"

config: config.host

config.host:
	$(call py_host_run,scripts/project_config.py $(_CONFIG_PY_ARGS) dump)

config.deployment:
	$(call py_run,scripts/deployment_config.py $(_CONFIG_PY_ARGS) dump)

config.labs:
	$(call py_run,scripts/lab_registry.py $(_CONFIG_PY_ARGS) dump)

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

lab.help:
	$(call py_run,scripts/lab_host.py list)

lab.%.help:
	$(call py_run,scripts/lab_host.py help $*)

lab.%.parse:
	$(call py_run,scripts/lab_host.py parse $* $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.scripted.build:
	$(call py_run,scripts/lab_host.py build $* --variant scripted)

lab.%.scripted.clean:
	$(call py_run,scripts/lab_host.py clean $* --variant scripted)

lab.%.scripted.flash:
	$(call py_run,scripts/lab_host.py flash $* --variant scripted $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

lab.%.scripted.capture:
	$(call py_run,scripts/lab_host.py capture $* --variant scripted $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.scripted:
	$(call py_run,scripts/lab_host.py run $* --variant $(if $(filter local,$(BACKEND)),scripted,scripted-phaseb) $(if $(BACKEND),--backend "$(BACKEND)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.phaseb.build:
	$(call py_run,scripts/lab_host.py build $* --variant phaseb $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

lab.%.phaseb.clean:
	$(call py_run,scripts/lab_host.py clean $* --variant phaseb)

lab.%.phaseb.flash:
	$(call py_run,scripts/lab_host.py flash $* --variant phaseb $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

lab.%.phaseb.capture:
	$(call py_run,scripts/lab_host.py capture $* --variant phaseb $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.phaseb:
	$(call py_run,scripts/lab_host.py run $* --variant phaseb $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.eager.build:
	$(call py_run,scripts/lab_host.py build $* --variant eager)

lab.%.eager.clean:
	$(call py_run,scripts/lab_host.py clean $* --variant eager)

lab.%.eager.flash:
	$(call py_run,scripts/lab_host.py flash $* --variant eager $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

lab.%.eager.capture:
	$(call py_run,scripts/lab_host.py capture $* --variant eager $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.eager:
	$(call py_run,scripts/lab_host.py run $* --variant eager $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.build:
	$(call py_run,scripts/lab_host.py build $* $(if $(VARIANT),--variant "$(VARIANT)",))

lab.%.clean:
	$(call py_run,scripts/lab_host.py clean $* $(if $(VARIANT),--variant "$(VARIANT)",))

lab.%.flash:
	$(call py_run,scripts/lab_host.py flash $* $(if $(VARIANT),--variant "$(VARIANT)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

lab.%.capture:
	$(call py_run,scripts/lab_host.py capture $* $(if $(VARIANT),--variant "$(VARIANT)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%:
	$(call py_run,scripts/lab_host.py run $* $(if $(VARIANT),--variant "$(VARIANT)",) $(if $(BACKEND),--backend "$(BACKEND)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))
