# Host tasks for the ESP32-S3-BOX-3 on USB + demo runners.
# GNU Make + Python 3.9+. Override the port with PORT=...
#
# Plug USB-C into the box itself. The dock USB-C jack is power only.

ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
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
RUN_CROSS_HEARTBEAT := $(ROOT)/scripts/run_cross_heartbeat.py

# Device firmware demo id (h02, h05, p01, …). See `make flash-list`.
DEMO ?= h02

ifdef PORT
export ESPPORT := $(PORT)
endif

export PYTHONUNBUFFERED := 1

.DEFAULT_GOAL := help

.PHONY: help install install-server install-persona connect system storage report \
	device-detect device-port device-sync device-chip \
	device-os device-software device-env \
	device-flash device-partitions device-fs device-data \
	idf-install flash flash-list flash-monitor build-firmware monitor \
	h01 h02 h03 h04 h05 h06 h07 h08 h09 h10 h11 h12 h13 h14 h15 h16 h17 \
	x01 \
	p01 p02 p03 p04 p05 p06 p07 p08 p09 p10 p11 \
	demo-auth demo-heartbeat demo-messages demo-cursor \
	demo-hangout demo-relay demo-combined demos-server \
	demo-cross-heartbeat \
	install-persona demo-capture demo-cut demo-record demo-ideas demo-pack \
	demos-persona

help:
	@printf '%s\n' \
	'' \
	'Family desk link — ESP32-S3-BOX-3 host tasks' \
	'' \
	'  make                 this help' \
	'  make install         Python venv + esptool from PyPI' \
	'  make install-server  same, plus FastAPI/httpx for protocol demos' \
	'  make install-persona Pillow for parent photo/voice packing' \
	'' \
	'Inspect (USB, no IDF):' \
	'  make connect         USB detect → port → ROM sync → chip' \
	'  make system          OS, firmware versions, host/device environment' \
	'  make storage         flash size, partitions, filesystems' \
	'  make report          connect, then system, then storage' \
	'' \
	'Flash (needs ESP-IDF; USB-C on the box, not the dock):' \
	'  make idf-install     clone ESP-IDF to ~/esp/esp-idf and install esp32s3 tools' \
	'  make flash DEMO=h02  build+flash one firmware demo' \
	'  make flash-monitor DEMO=h02   flash then serial monitor' \
	'  make build-firmware DEMO=h02  compile only' \
	'  make monitor         serial monitor (115200 / idf.py)' \
	'  make flash-list      which demo .c files exist' \
	'  make h01 … h17       aliases (h01 = Espressif BSP example; h16 = HTTPS; h17 = cross-Wi-Fi heartbeat)' \
	'  make x01             product shell (locked / PIN / inbox / hangout)' \
	'  make p01 … p11       personality / face / packed portrait' \
	'' \
	'Server protocol demos (no box required):' \
	'  make demo-auth       01 known-device bearer tokens' \
	'  make demo-heartbeat  02 presence / stale / recover' \
	'  make demo-messages   03 text + WAV store-and-forward' \
	'  make demo-cursor     04 playhead + archive after reboot' \
	'  make demo-hangout    05 invite/ring/accept/floor/hangup' \
	'  make demo-relay      06 PCM copy-through' \
	'  make demo-combined   glue host (REST + hangout WS + /app)' \
	'  make demos-server    run 01–06 in order' \
	'' \
	'Box + server on different Wi-Fi (heartbeat):' \
	'  make demo-cross-heartbeat     bind 0.0.0.0, flash h17, beat both ways' \
	'  make demo-cross-heartbeat TUNNEL=1          public HTTPS via cloudflared' \
	'  make demo-cross-heartbeat SERVER_HOST=IP    bake a Tailscale/LAN host' \
	'  Box joins 2.4 GHz (firmware/secrets.h). This Mac can sit on another SSID.' \
	'' \
	'Parent likeness (no family media required; stand-ins until you pass --in):' \
	'  make demo-capture    still: generated SAMPLE, or --in / --webcam' \
	'  make demo-cut        square 200px crop' \
	'  make demo-record     greeting WAV (hum, or --mic / --in)' \
	'  make demo-ideas      stylized variants to pick a look later' \
	'  make demo-pack       PNG+WAV → C arrays for p10/p11' \
	'  make demos-persona   run a01–a05' \
	'  make flash DEMO=p10  show packed portrait' \
	'  make flash DEMO=p11  play packed greeting' \
	'' \
	'Parent Mac (phone stand-in; audio through the server, not box-to-box Wi-Fi):' \
	'  python demos/parent/live_ptt.py --base-url http://MAC:8080' \
	'  python demos/parent/send_voicemail.py --base-url http://MAC:8080' \
	'  python demos/parent/send_photo.py --base-url http://MAC:8080' \
	'  python scripts/dev_https.py     TLS certs + HTTPS :8443 (docs/TLS.md)' \
	'  See docs/DEMO-MAP.md' \
	'' \
	'Port: make flash DEMO=h02 PORT=/dev/cu.usbmodem113401' \
	'Cable: USB-C on the box, not the dock.' \
	''

install:
	@$(INSTALL_PY) "$(INSTALL)"

install-server:
	@$(INSTALL_PY) "$(INSTALL)" --server

install-persona:
	@$(INSTALL_PY) "$(INSTALL)" --persona

connect:
	@$(PYTHON) "$(DEVICE)" connect

system:
	@$(PYTHON) "$(DEVICE)" system

storage:
	@$(PYTHON) "$(DEVICE)" storage

report:
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

idf-install:
	@$(PYTHON) "$(FLASH)" --idf-install

flash-list:
	@$(PYTHON) "$(FLASH)" --list

flash:
	@$(PYTHON) "$(FLASH)" --demo "$(DEMO)"

flash-monitor:
	@$(PYTHON) "$(FLASH)" --demo "$(DEMO)" --monitor

build-firmware:
	@$(PYTHON) "$(FLASH)" --demo "$(DEMO)" --build-only

monitor:
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

x01:
	@$(PYTHON) "$(FLASH)" --demo x01

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

demo-auth:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 01_auth

demo-heartbeat:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 02_heartbeat

demo-messages:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 03_messages

demo-cursor:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 04_cursor_archive

demo-hangout:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 05_hangout_signaling

demo-relay:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 06_audio_relay

demo-combined:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" combined

# Box on 2.4 GHz, server on this Mac (possibly another SSID). See scripts/run_cross_heartbeat.py.
CROSS_HEARTBEAT_ARGS :=
ifeq ($(TUNNEL),1)
CROSS_HEARTBEAT_ARGS += --tunnel
endif
ifdef SERVER_HOST
CROSS_HEARTBEAT_ARGS += --host $(SERVER_HOST)
endif
ifdef SERVER_PORT
CROSS_HEARTBEAT_ARGS += --port $(SERVER_PORT)
endif
ifeq ($(SKIP_FLASH),1)
CROSS_HEARTBEAT_ARGS += --skip-flash
endif
ifeq ($(NO_PEER),1)
CROSS_HEARTBEAT_ARGS += --no-peer
endif
ifdef BIND_PORT
CROSS_HEARTBEAT_ARGS += --bind-port $(BIND_PORT)
endif
ifdef PORT
CROSS_HEARTBEAT_ARGS += --serial $(PORT)
endif

demo-cross-heartbeat:
	@$(PYTHON) "$(RUN_CROSS_HEARTBEAT)" $(CROSS_HEARTBEAT_ARGS)

demos-server: demo-auth demo-heartbeat demo-messages demo-cursor demo-hangout demo-relay
	@echo '-- PASS demos-server'

demo-capture:
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 01_capture

demo-cut:
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 02_cut

demo-record:
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 03_record

demo-ideas:
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 04_ideas

demo-pack:
	@$(PYTHON) "$(RUN_PERSONA_DEMO)" 05_pack

demos-persona:
	@$(PYTHON) "$(RUN_PERSONA_DEMO)"
