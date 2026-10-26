# Family Link — active repo host tasks (GNU Make + Python 3.9+).
# Windows: Git for Windows bash for recipes (see porcelain-style GITBASH below).
# macOS: /bin/bash. Daily shells: PowerShell 7 (Windows), zsh (macOS) — docs/AGENTS.md.

ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

ifeq ($(OS),Windows_NT)
  EMPTY :=
  SPACE := $(EMPTY) $(EMPTY)
  # wildcard splits on spaces. Escape them after turning backslashes into slashes.
  _win_path = $(subst $(SPACE),\ ,$(subst \,/,$(1)))
  ifeq ($(origin GITBASH),undefined)
    # ProgramW6432 is not a reliable make variable: a copied Windows environment
    # can spell it in a different case, and GNU make looks names up exactly.
    _GIT_BASH := $(wildcard C:/Program\ Files/Git/bin/bash.exe)
    ifeq ($(_GIT_BASH),)
      _X86_GIT := C:/Program Files (x86)/Git/bin/bash.exe
      _GIT_BASH := $(wildcard $(call _win_path,$(_X86_GIT)))
    endif
    ifeq ($(_GIT_BASH),)
      _GIT_BASH := $(wildcard $(call _win_path,$(LOCALAPPDATA)/Programs/Git/bin/bash.exe))
    endif
    ifeq ($(_GIT_BASH),)
      $(error Git for Windows bash was not found. Install Git or set GITBASH= to bash.exe)
    endif
    GITBASH := "$(_GIT_BASH)"
  endif
  # This GNU make looks for sh.exe before it can run a recipe. Recipes invoke
  # Git bash themselves, so cmd.exe only has to launch that quoted path.
  # SystemRoot has the same case-sensitive lookup problem as ProgramW6432.
  _CMD := $(wildcard C:/Windows/System32/cmd.exe)
  ifeq ($(_CMD),)
    _CMD := $(wildcard $(call _win_path,$(SystemRoot)/System32/cmd.exe))
  endif
  ifeq ($(_CMD),)
    _CMD := $(wildcard $(call _win_path,$(SYSTEMROOT)/System32/cmd.exe))
  endif
  SHELL := $(_CMD)
  .SHELLFLAGS := /c
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

.PHONY: help config config.host config.deployment config.labs install test build devices flash flash.all device.bind avatars app app\:build app\:flash app\:monitor app\:test server lab.help lab.p1.server lab.lab02.server lab.lab03.server lab.lab07.server lab.lab08.server lab.lab09.server lab.lab11.server

# Lab device firmware (registry: config/labs.registry.yaml). Optional: KIT PORT SECONDS FLASH RUN_ID
# Prefer the explicit .scripted profile over VARIANT= for operator workflows.
.PHONY: lab.%.build lab.%.flash lab.%.capture lab.%.clean lab.%.help
.PHONY: lab.%.scripted lab.%.scripted.build lab.%.scripted.flash lab.%.scripted.capture lab.%.scripted.clean
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

avatars:
	$(call step_msg,Packing profile avatars for firmware)
	$(call py_run,scripts/pack_profile_avatars.py)

test:
	$(call step_msg,Testing project configuration)
	$(call py_run,-m unittest discover -s scripts/tests)

build:
	$(call py_run,scripts/firmware.py build)

app:
	$(call step_msg,Building and flashing v2 product app (client/device))
	$(call py_run,scripts/app_host.py build $(if $(KIT),--kit "$(KIT)",) $(if $(P1_HOST),--p1-host "$(P1_HOST)",))
	$(call py_run,scripts/app_host.py flash $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

app\:build:
	$(call step_msg,Building v2 product app firmware (client/device))
	$(call py_run,scripts/app_host.py build $(if $(KIT),--kit "$(KIT)",) $(if $(P1_HOST),--p1-host "$(P1_HOST)",))

app\:flash:
	$(call step_msg,Flashing v2 product app to one registered kit)
	$(call py_run,scripts/app_host.py flash $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

app\:monitor:
	$(call step_msg,Serial monitor for v2 product app (client/device))
	$(call py_run,scripts/app_host.py monitor $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(FILTER),--print-filter "$(FILTER)",))

app\:test:
	$(call step_msg,Running v2 product app host tests (see config/app-test-suite.yaml))
	$(call py_run,scripts/app_test_suite.py)

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

server lab.p1.server:
	$(call step_msg,P1 core-journey wire server on 0.0.0.0:8777)
	$(call py_host_run,server/device/main/server.py --bind 0.0.0.0 --port 8777 --run-id local-dev $(if $(SERVER_TLS_CERT),--tls-cert "$(SERVER_TLS_CERT)",) $(if $(SERVER_TLS_KEY),--tls-key "$(SERVER_TLS_KEY)",) $(if $(filter 1 true yes,$(SERVER_PRODUCTION)),--production,))

lab.lab02.server:
	$(call py_host_run,server/labs/lab-02-paging/server.py --port 8765)

lab.lab03.server:
	$(call py_host_run,server/labs/lab-03-media/server.py --bind 0.0.0.0 --port 8766 --run-id standalone)

lab.lab07.server:
	$(call py_host_run,server/labs/lab-07-readiness-roster/server.py --bind 0.0.0.0 --port 8767 --run-id standalone)

lab.lab08.server:
	$(call py_host_run,server/labs/lab-08-pin-cooldown/server.py --bind 0.0.0.0 --port 8768 --run-id standalone)

lab.lab09.server:
	$(call py_host_run,server/labs/lab-09-session-isolation/server.py --bind 0.0.0.0 --port 8769 --run-id standalone)

lab.lab11.server:
	$(call py_host_run,server/labs/lab-11-send-receipt/server.py --bind 0.0.0.0 --port 8770 --run-id standalone)

lab.%.help:
	$(call py_run,scripts/lab_host.py help $*)

lab.%.parse:
	$(call py_run,scripts/lab_host.py parse $* $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.compare:
ifndef RUN_A
	$(error lab.$*.compare requires RUN_A and RUN_B, e.g. make lab.lab16.compare RUN_A=run-one RUN_B=run-two)
endif
ifndef RUN_B
	$(error lab.$*.compare requires RUN_A and RUN_B, e.g. make lab.lab16.compare RUN_A=run-one RUN_B=run-two)
endif
	$(call py_run,scripts/lab_host.py compare $* --run-a "$(RUN_A)" --run-b "$(RUN_B)")

lab.lab16.results:
	$(call py_run,client/labs/lab-16-attached-storage/tools/render_memory_results.py)

lab.%.scripted.build:
	$(call py_run,scripts/lab_host.py build $* --variant scripted $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

lab.%.scripted.clean:
	$(call py_run,scripts/lab_host.py clean $* $(if $(VARIANT),--variant "$(VARIANT)",))

lab.%.scripted.flash:
	$(call py_run,scripts/lab_host.py flash $* $(if $(VARIANT),--variant "$(VARIANT)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

lab.%.scripted.capture:
	$(call py_run,scripts/lab_host.py capture $* --mode scripted $(if $(VARIANT),--variant "$(VARIANT)",) $(if $(BACKEND),--backend "$(BACKEND)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.scripted:
	$(call py_run,scripts/lab_host.py run $* --mode scripted $(if $(VARIANT),--variant "$(VARIANT)",) $(if $(BACKEND),--backend "$(BACKEND)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%.build:
	$(call py_run,scripts/lab_host.py build $* $(if $(VARIANT),--variant "$(VARIANT)",))

lab.%.clean:
	$(call py_run,scripts/lab_host.py clean $* $(if $(VARIANT),--variant "$(VARIANT)",))

lab.%.flash:
	$(call py_run,scripts/lab_host.py flash $* $(if $(VARIANT),--variant "$(VARIANT)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",))

lab.%.capture:
	$(call py_run,scripts/lab_host.py capture $* $(if $(VARIANT),--variant "$(VARIANT)",) $(if $(BACKEND),--backend "$(BACKEND)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))

lab.%:
	$(call py_run,scripts/lab_host.py run $* $(if $(VARIANT),--variant "$(VARIANT)",) $(if $(BACKEND),--backend "$(BACKEND)",) $(if $(KIT),--kit "$(KIT)",) $(if $(PORT),--port "$(PORT)",) $(if $(SECONDS),--seconds "$(SECONDS)",) $(if $(filter 0 false no,$(FLASH)),--no-flash,) $(if $(filter 1 true yes,$(FLASH)),--flash,) $(if $(RUN_ID),--run-id "$(RUN_ID)",))
