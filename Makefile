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

# Device firmware demo id (h02, h05, p01, …). See `make flash-list`.
DEMO ?= h02

ifdef PORT
export ESPPORT := $(PORT)
endif
ifdef WHO
export WHO
export FAMILY_WHO := $(WHO)
endif

export PYTHONUNBUFFERED := 1

.DEFAULT_GOAL := help

.PHONY: help install install-server install-persona connect system storage report \
	device-detect device-port device-sync device-chip \
	device-os device-software device-env \
	device-flash device-partitions device-fs device-data \
	idf-install flash flash-list flash-monitor build-firmware monitor \
	h01 h02 h03 h04 h05 h06 h07 h08 h09 h10 h11 h12 h13 h14 h15 h16 h17 \
	h18 h19 h20 h21 h22 h23 h24 h25 h26 h27 h28 h29 \
	x01 x02 \
	p01 p02 p03 p04 p05 p06 p07 p08 p09 p10 p11 p12 \
	demo-auth demo-heartbeat demo-messages demo-cursor demo-playback \
	demo-hangout demo-relay demo-presence demo-talk demo-diary demo-device-log \
	demo-draw demo-sketch demo-pingpong \
	demo-combined demos-server \
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
	'  make flash DEMO=h26 WHO=mazi   (second kit: WHO=arlo; compile once)' \
	'  make flash DEMO=h27 WHO=mazi   sketch note (async drawing; WHO=arlo)' \
	'  make flash DEMO=h26 WHO=mazi PORT=/dev/cu.usbmodem…  (first bind only)' \
	'  make flash-monitor DEMO=h02   flash then serial monitor' \
	'  make build-firmware DEMO=h02  compile only' \
	'  make monitor         serial monitor (115200 / idf.py)' \
	'  make flash-list      which demo .c files exist' \
	'  make h01 … h28       aliases (h01 = Espressif BSP example; h20–h22 / h26 / h27 = Mazi/Arlo two-box; h28 = short LCD clip)' \
	'  make x01             legacy product shell (peer inbox)' \
	'  make x02             v1 carousel shell (hangout users, PIN, inbox)' \
	'  make v1-server       leave v1 product host running on :8080' \
	'  make v1-server-tls   v1 host on :8443 (needs data/certs/dev*.pem)' \
	'  make p01 … p11       personality / face / packed portrait' \
	'' \
	'Server protocol demos (no box required):' \
	'  make demo-auth       01 known-device bearer tokens' \
	'  make demo-heartbeat  02 presence / stale / recover' \
	'  make demo-messages   03 text + WAV store-and-forward' \
	'  make demo-playback   h18 fixture smoke test (starts+stops)' \
	'  python demos/server/h18_playback/server.py --host 0.0.0.0 --port 8080' \
	'  open http://localhost:8080/box/     320x240 web twin (same catalog)' \
	'  make demo-cursor     04 playhead + archive after reboot' \
	'  make demo-hangout    05 invite/ring/accept/floor/hangup' \
	'  make demo-relay      06 PCM copy-through' \
	'  make demo-presence   h20 mute open/away heartbeat (two twins)' \
	'  make demo-talk       h21 full-duplex PCM copy (two twins)' \
	'  make demo-diary      h22 dated diary chunks' \
	'  make demo-device-log h24 heartbeat + device log upload' \
	'  make demo-draw       h26 shared drawing (two twins, heart polyline)' \
	'  make demo-sketch     h27 drawing note (store-and-forward timed strokes)' \
	'  make demo-pingpong   h29 async voice ping-pong (two twins)' \
	'  make demo-combined   glue host (REST + hangout WS + /app)' \
	'  make demo-v1         v1 product host smoke (hangout users + inbox)' \
	'  make demos-server    run 01–06 in order' \
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
	'Port: make flash DEMO=h02 PORT=/dev/cu.usbmodem113401  (first bind only if two kits)' \
	'Who:  make flash DEMO=h27 WHO=mazi   then   WHO=arlo  (one compile; USB serial remembered)' \
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

x01:
	@$(PYTHON) "$(FLASH)" --demo x01

x02:
	@$(PYTHON) "$(FLASH)" --demo x02

v1-server:
	@$(PYTHON) -m demos.server.v1_product.server --host 0.0.0.0 --port 8080

v1-server-tls:
	@$(PYTHON) -m demos.server.v1_product.server --host 0.0.0.0 --port 8443 \
		--ssl-certfile data/certs/dev.pem --ssl-keyfile data/certs/dev-key.pem

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

demo-auth:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 01_auth

demo-heartbeat:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 02_heartbeat

demo-messages:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 03_messages

demo-playback:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h18_playback

demo-cursor:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 04_cursor_archive

demo-hangout:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 05_hangout_signaling

demo-relay:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" 06_audio_relay

demo-presence:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h20_presence

demo-talk:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h21_talk

demo-diary:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h22_diary

demo-device-log:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h24_device_log

demo-draw:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h26_draw

demo-sketch:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h27_sketch

demo-pingpong:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" h29_pingpong

demo-combined:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" combined

demo-v1:
	@$(PYTHON) "$(RUN_SERVER_DEMO)" v1_product

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
