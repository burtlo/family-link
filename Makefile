# Family-link Makefile — host tasks for ESP32-S3-BOX-3 on USB + demo runners.
# GNU Make + Python 3.9+. Override the port with PORT=...
# Plug USB-C into the box itself. The dock USB-C jack is power only.

SHELL := /bin/bash

ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

define step_msg
	@echo '[STEP] $(1)'
endef
VENV_PY := $(firstword \
	$(wildcard $(ROOT)/.venv/bin/python3) \
	$(wildcard $(ROOT)/.venv/bin/python) \
	$(wildcard $(ROOT)/.venv/Scripts/python.exe))
PYTHON := $(if $(VENV_PY),$(VENV_PY),python3)
INSTALL_PY := $(firstword \
	$(wildcard $(ROOT)/.venv/bin/python3) \
	$(wildcard $(ROOT)/.venv/bin/python) \
	$(wildcard $(ROOT)/.venv/Scripts/python.exe) \
	$(shell command -v python3 2>/dev/null) \
	$(shell command -v python 2>/dev/null) \
	python3)
DEVICE := $(ROOT)/scripts/device.py
INSTALL := $(ROOT)/scripts/install.py
FLASH := $(ROOT)/scripts/flash.py
RUN_SERVER_DEMO := $(ROOT)/scripts/run_server_demo.py
RUN_PERSONA_DEMO := $(ROOT)/scripts/run_persona_demo.py

# Device firmware demo id (h02, h05, p01, …). See `make flash-list`.
DEMO ?= h02

ifdef PORT
export ESPPORT := $(PORT)
endif
ifdef WHO
export WHO
export FAMILY_WHO := $(WHO)
endif
ifdef PROFILE
export PROFILE
export SECRETS_PROFILE := $(PROFILE)
endif

export PYTHONUNBUFFERED := 1

.DEFAULT_GOAL := help

.PHONY: help install install-server install-persona connect system storage report \
	device-detect device-port device-sync device-chip \
	device-os device-software device-env \
	device-flash device-partitions device-fs device-data \
	idf-install flash flash-list flash-monitor build-firmware monitor \
	v1-timing check-v1-parity \
	h01 h02 h03 h04 h05 h06 h07 h08 h09 h10 h11 h12 h13 h14 h15 h16 h17 \
	h18 h19 h20 h21 h22 h23 h24 h25 h26 h27 h28 h29 h30 h31 \
	x01 x02 \
	p01 p02 p03 p04 p05 p06 p07 p08 p09 p10 p11 p12 p13 \
	v1-server v1-server-tls demo-v1 \
	demo-auth demo-heartbeat demo-messages demo-cursor demo-playback demo-message-store \
	demo-hangout demo-relay demo-presence demo-talk demo-diary demo-device-log \
	demo-draw demo-sketch demo-pingpong \
	demo-combined demos-server \
	install-persona demo-capture demo-cut demo-record demo-ideas demo-pack \
	demos-persona

help:
	@bash "$(ROOT)/scripts/make-help.sh"

# --- Install ---

install:
	$(call step_msg,Installing Python venv and esptool)
	@$(INSTALL_PY) "$(INSTALL)"

install-server:
	$(call step_msg,Installing host deps for server protocol demos)
	@$(INSTALL_PY) "$(INSTALL)" --server

install-persona:
	$(call step_msg,Installing Pillow for parent photo/voice packing)
	@$(INSTALL_PY) "$(INSTALL)" --persona

# --- Device inspect (USB, no IDF) ---

connect:
	$(call step_msg,Connecting to box over USB)
	@$(PYTHON) "$(DEVICE)" connect

system:
	$(call step_msg,Reading system and firmware versions)
	@$(PYTHON) "$(DEVICE)" system

storage:
	$(call step_msg,Reading flash partitions and filesystems)
	@$(PYTHON) "$(DEVICE)" storage

report:
	$(call step_msg,Device report (connect + system + storage))
	@$(PYTHON) "$(DEVICE)" report

device-detect:
	@$(PYTHON) "$(DEVICE)" device-detect

device-port:
	@$(PYTHON) "$(DEVICE)" device-port

device-sync:
	@$(PYTHON) "$(DEVICE)" device-sync

device-chip:
	@$(PYTHON) "$(DEVICE)" device-chip

device-os:
	@$(PYTHON) "$(DEVICE)" device-os

device-software:
	@$(PYTHON) "$(DEVICE)" device-software

device-env:
	@$(PYTHON) "$(DEVICE)" device-env

device-flash:
	@$(PYTHON) "$(DEVICE)" device-flash

device-partitions:
	@$(PYTHON) "$(DEVICE)" device-partitions

device-fs:
	@$(PYTHON) "$(DEVICE)" device-fs

device-data:
	@$(PYTHON) "$(DEVICE)" device-data

# --- Flash (ESP-IDF) ---

idf-install:
	$(call step_msg,Installing ESP-IDF and esp32s3 tools)
	@$(PYTHON) "$(FLASH)" --idf-install

flash-list:
	$(call step_msg,Listing firmware demo sources)
	@$(PYTHON) "$(FLASH)" --list

flash:
	$(call step_msg,Building and flashing firmware demo $(DEMO))
	@$(PYTHON) "$(FLASH)" --demo "$(DEMO)"

flash-monitor:
	$(call step_msg,Flash $(DEMO) then open serial monitor)
	@$(PYTHON) "$(FLASH)" --demo "$(DEMO)" --monitor

build-firmware:
	$(call step_msg,Building firmware demo $(DEMO) (no flash))
	@$(PYTHON) "$(FLASH)" --demo "$(DEMO)" --build-only

monitor:
	$(call step_msg,Serial monitor (115200))
	@$(PYTHON) "$(FLASH)" --monitor

h01:
	@$(PYTHON) "$(FLASH)" --demo h01

h02:
	@$(PYTHON) "$(FLASH)" --demo h02

h03:
	@$(PYTHON) "$(FLASH)" --demo h03

h04:
	@$(PYTHON) "$(FLASH)" --demo h04

h05:
	@$(PYTHON) "$(FLASH)" --demo h05

h06:
	@$(PYTHON) "$(FLASH)" --demo h06

h07:
	@$(PYTHON) "$(FLASH)" --demo h07

h08:
	@$(PYTHON) "$(FLASH)" --demo h08

h09:
	@$(PYTHON) "$(FLASH)" --demo h09

h10:
	@$(PYTHON) "$(FLASH)" --demo h10

h11:
	@$(PYTHON) "$(FLASH)" --demo h11

h12:
	@$(PYTHON) "$(FLASH)" --demo h12

h13:
	@$(PYTHON) "$(FLASH)" --demo h13

h14:
	@$(PYTHON) "$(FLASH)" --demo h14

h15:
	@$(PYTHON) "$(FLASH)" --demo h15

h16:
	@$(PYTHON) "$(FLASH)" --demo h16

h17:
	@$(PYTHON) "$(FLASH)" --demo h17

h18:
	@$(PYTHON) "$(FLASH)" --demo h18

h19:
	@$(PYTHON) "$(FLASH)" --demo h19

h20:
	@$(PYTHON) "$(FLASH)" --demo h20

h21:
	@$(PYTHON) "$(FLASH)" --demo h21

h22:
	@$(PYTHON) "$(FLASH)" --demo h22

h23:
	@$(PYTHON) "$(FLASH)" --demo h23

h24:
	@$(PYTHON) "$(FLASH)" --demo h24

h25:
	@$(PYTHON) "$(FLASH)" --demo h25

h26:
	@$(PYTHON) "$(FLASH)" --demo h26

h27:
	@$(PYTHON) "$(FLASH)" --demo h27

h28:
	@$(PYTHON) "$(FLASH)" --demo h28

h29:
	@$(PYTHON) "$(FLASH)" --demo h29

h30:
	@$(PYTHON) "$(FLASH)" --demo h30

h31:
	@$(PYTHON) "$(FLASH)" --demo h31

demo-opus-messages:
	$(call step_msg,h31 Opus chunk server smoke test)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h31_opus_messages

demo-message-store:
	$(call step_msg,h34 canonical message_store PCM smoke test)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h34_message_store

x01:
	@$(PYTHON) "$(FLASH)" --demo x01

# --- V1 device (carousel firmware) ---

x02:
	$(call step_msg,Building and flashing v1 device firmware (x02))
	@$(PYTHON) "$(FLASH)" --demo x02

v1-timing:
	$(call step_msg,Regenerating v1_timing.h and v1_timing.js from timing.yaml)
	@$(PYTHON) shared/v1/gen_timing.py

check-v1-parity:
	$(call step_msg,Checking timing.yaml vs firmware vs web twin)
	@$(PYTHON) scripts/check_v1_parity.py

# --- V1 server (product host) ---

v1-server:
	$(call step_msg,Running v1 product host on 0.0.0.0:8080)
	@$(PYTHON) -m demos.server.v1_product.server --host 0.0.0.0 --port 8080

v1-server-tls:
	$(call step_msg,Running v1 product host on 0.0.0.0:8443 (TLS))
	@$(PYTHON) -m demos.server.v1_product.server --host 0.0.0.0 --port 8443 \
		--ssl-certfile data/certs/dev.pem --ssl-keyfile data/certs/dev-key.pem

demo-v1:
	$(call step_msg,V1 server smoke test (hangout users + inbox))
	@$(PYTHON) "$(RUN_SERVER_DEMO)" v1_product

p01:
	@$(PYTHON) "$(FLASH)" --demo p01

p02:
	@$(PYTHON) "$(FLASH)" --demo p02

p03:
	@$(PYTHON) "$(FLASH)" --demo p03

p04:
	@$(PYTHON) "$(FLASH)" --demo p04

p05:
	@$(PYTHON) "$(FLASH)" --demo p05

p06:
	@$(PYTHON) "$(FLASH)" --demo p06

p07:
	@$(PYTHON) "$(FLASH)" --demo p07

p08:
	@$(PYTHON) "$(FLASH)" --demo p08

p09:
	@$(PYTHON) "$(FLASH)" --demo p09

p10:
	@$(PYTHON) "$(FLASH)" --demo p10

p11:
	@$(PYTHON) "$(FLASH)" --demo p11

p12:
	@$(PYTHON) "$(FLASH)" --demo p12

p13:
	@$(PYTHON) "$(FLASH)" --demo p13

# --- Server protocol demos ---

demo-auth:
	$(call step_msg,Server demo 01 — known-device bearer tokens)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 01_auth

demo-heartbeat:
	$(call step_msg,Server demo 02 — presence / stale / recover)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 02_heartbeat

demo-messages:
	$(call step_msg,Server demo 03 — text + WAV store-and-forward)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 03_messages

demo-playback:
	$(call step_msg,Server demo h18 — playback fixture smoke test)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h18_playback

demo-cursor:
	$(call step_msg,Server demo 04 — playhead + archive after reboot)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 04_cursor_archive

demo-hangout:
	$(call step_msg,Server demo 05 — invite/ring/accept/floor/hangup)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 05_hangout_signaling

demo-relay:
	$(call step_msg,Server demo 06 — PCM copy-through)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 06_audio_relay

demo-presence:
	$(call step_msg,Server demo h20 — mute open/away heartbeat)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h20_presence

demo-talk:
	$(call step_msg,Server demo h21 — full-duplex PCM copy)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h21_talk

demo-diary:
	$(call step_msg,Server demo h22 — dated diary chunks)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h22_diary

demo-device-log:
	$(call step_msg,Server demo h24 — heartbeat + device log upload)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h24_device_log

demo-draw:
	$(call step_msg,Server demo h26 — shared drawing)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h26_draw

demo-sketch:
	$(call step_msg,Server demo h27 — drawing note)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h27_sketch

demo-pingpong:
	$(call step_msg,Server demo h29 — async voice ping-pong)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h29_pingpong

demo-combined:
	$(call step_msg,Server demo combined — REST + hangout WS + /app)
	@$(PYTHON) "$(RUN_SERVER_DEMO)" combined

demos-server:
	$(call step_msg,Running server protocol demos 01–06 in order)
	@$(MAKE) --no-print-directory demo-auth demo-heartbeat demo-messages demo-cursor demo-hangout demo-relay
	@echo '[PASS] demos-server'

# --- Parent likeness demos ---

demo-capture:
	$(call step_msg,Persona demo a01 — capture still)
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 01_capture

demo-cut:
	$(call step_msg,Persona demo a02 — square crop)
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 02_cut

demo-record:
	$(call step_msg,Persona demo a03 — greeting WAV)
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 03_record

demo-ideas:
	$(call step_msg,Persona demo a04 — stylized variants)
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 04_ideas

demo-pack:
	$(call step_msg,Persona demo a05 — PNG+WAV to C arrays)
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 05_pack

demos-persona:
	$(call step_msg,Running persona demos a01–a05)
	@$(PYTHON) "$(RUN_PERSONA_DEMO)"
