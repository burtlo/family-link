"use strict";

const STORAGE = {
  token: "token",
  deviceId: "device_id",
  origin: "origin",
};

const DEFAULTS = {
  deviceId: "box-b",
  token: "change-me-b",
  origin: "http://localhost:8080",
};

const HEARTBEAT_MS = 4000;
const PCM_RATE = 16000;
const FRAME_SAMPLES = 320;
const FRAME_BYTES = 640;
const VOICEMAIL_MAX_S = 60;

const $ = (id) => document.getElementById(id);

const ui = {
  settings: $("settings"),
  origin: $("origin"),
  deviceId: $("device-id"),
  token: $("token"),
  connect: $("connect"),
  who: $("who-label"),
  peer: $("stat-peer"),
  onlineText: $("stat-online-text"),
  lamp: $("lamp-online"),
  unread: $("stat-unread"),
  playhead: $("stat-playhead"),
  statusLine: $("status-line"),
  micNote: $("mic-note"),
  hangoutLine: $("hangout-line"),
  invite: $("invite"),
  accept: $("accept"),
  hangup: $("hangup"),
  ptt: $("ptt"),
  pttState: $("ptt-state"),
  textForm: $("text-form"),
  noteText: $("note-text"),
  sendText: $("send-text"),
  record: $("record"),
  wavFile: $("wav-file"),
  photoFile: $("photo-file"),
  composeLine: $("compose-line"),
  refresh: $("refresh"),
  inbox: $("inbox"),
};

const state = {
  connected: false,
  wsWanted: false,
  ws: null,
  heartbeat: null,
  me: null,
  hangout: "idle",
  floor: null,
  holding: false,
  speakerMuted: false,
  audioCtx: null,
  playTime: 0,
  capture: null,
  mediaStream: null,
  recorder: null,
  recordChunks: [],
  recordTimer: null,
  recordStarted: 0,
  recordStream: null,
  objectUrls: [],
  pttPointer: null,
};

function loadSettings() {
  ui.origin.value = localStorage.getItem(STORAGE.origin) || DEFAULTS.origin;
  ui.deviceId.value = localStorage.getItem(STORAGE.deviceId) || DEFAULTS.deviceId;
  ui.token.value = localStorage.getItem(STORAGE.token) || DEFAULTS.token;
}

function saveSettings() {
  localStorage.setItem(STORAGE.origin, ui.origin.value.trim());
  localStorage.setItem(STORAGE.deviceId, ui.deviceId.value.trim());
  localStorage.setItem(STORAGE.token, ui.token.value);
}

function deviceId() {
  return ui.deviceId.value.trim();
}

function token() {
  return ui.token.value;
}

function pageIsHttp() {
  return location.protocol === "http:" || location.protocol === "https:";
}

function configuredOrigin() {
  return ui.origin.value.trim().replace(/\/$/, "");
}

function useRelative() {
  if (!pageIsHttp()) return false;
  const origin = configuredOrigin();
  if (!origin) return true;
  try {
    const u = new URL(origin);
    if (u.host === location.host) return true;
    const local = u.hostname === "localhost" || u.hostname === "127.0.0.1";
    const pageLocal =
      location.hostname === "localhost" || location.hostname === "127.0.0.1";
    if (local && !pageLocal) return true;
  } catch {
    return true;
  }
  return false;
}

function httpBase() {
  return useRelative() ? "" : configuredOrigin();
}

function wsUrl() {
  if (useRelative()) {
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    return proto + "//" + location.host + "/v1/ws";
  }
  const origin = configuredOrigin();
  if (origin.startsWith("https://")) {
    return "wss://" + origin.slice("https://".length) + "/v1/ws";
  }
  if (origin.startsWith("http://")) {
    return "ws://" + origin.slice("http://".length) + "/v1/ws";
  }
  return origin + "/v1/ws";
}

function authHeaders(extra) {
  const headers = extra ? Object.assign({}, extra) : {};
  headers.Authorization = "Bearer " + token();
  return headers;
}

async function api(path, opts) {
  const options = opts ? Object.assign({}, opts) : {};
  options.headers = authHeaders(options.headers);
  const res = await fetch(httpBase() + path, options);
  const text = await res.text();
  let body = null;
  if (text) {
    try {
      body = JSON.parse(text);
    } catch {
      body = text;
    }
  }
  if (!res.ok) {
    const err = new Error(errorMessage(res.status, body));
    err.status = res.status;
    err.body = body;
    throw err;
  }
  return body;
}

function errorMessage(status, body) {
  if (body && typeof body === "object" && body.error) return String(body.error);
  if (typeof body === "string" && body) return body;
  if (status === 401) return "unauthorized";
  return "HTTP " + status;
}

function setLine(el, text) {
  el.textContent = text;
}

function setOnline(online) {
  ui.lamp.classList.toggle("on", !!online);
  ui.onlineText.textContent = online ? "yes" : "no";
}

function setMicNote(text) {
  if (!text) {
    ui.micNote.classList.add("hidden");
    ui.micNote.textContent = "";
    return;
  }
  ui.micNote.textContent = text;
  ui.micNote.classList.remove("hidden");
}

function secureMicHint() {
  if (!window.isSecureContext) {
    return (
      "This page is not a secure context, so the browser will not open the microphone. " +
      "Use HTTPS, or localhost on this Mac. On iPhone, http://LAN-IP is not enough. " +
      "Text, photo, and a WAV file still work."
    );
  }
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
    return (
      "This browser has no getUserMedia. Live talk and in-page recording need a mic. " +
      "Text, photo, and a WAV file still work."
    );
  }
  return "";
}

function updateChrome() {
  const me = deviceId() || "you";
  const peer = (state.me && state.me.peer_id) || "the box";
  ui.who.textContent = "You · " + me + " → " + peer;
  ui.connect.textContent = state.connected ? "Disconnect" : "Connect";
  const on = state.connected;
  ui.invite.disabled = !on || state.hangout !== "idle";
  ui.accept.disabled = !on || state.hangout !== "ringing";
  ui.hangup.disabled = !on || state.hangout === "idle";
  ui.ptt.disabled = !on || state.hangout !== "live";
  ui.sendText.disabled = !on;
  ui.record.disabled = !on;
  ui.wavFile.disabled = !on;
  ui.photoFile.disabled = !on;
  ui.refresh.disabled = !on;
  if (state.hangout !== "live") {
    ui.ptt.setAttribute("aria-pressed", "false");
    ui.pttState.textContent = "release = listen";
  }
}

function hangoutLabel() {
  if (state.hangout === "calling") return "Calling the box…";
  if (state.hangout === "ringing") return "Box is ringing. Accept to join.";
  if (state.hangout === "live") {
    const floor = state.floor ? "Floor: " + state.floor : "Floor open";
    const hold = state.holding ? " You are talking." : " Listening.";
    return "Live. " + floor + "." + hold + " Half-duplex.";
  }
  return "Idle. Half-duplex — one mouth at a time.";
}

function AudioCtx() {
  return window.AudioContext || window.webkitAudioContext;
}

async function ensureAudio() {
  const AC = AudioCtx();
  if (!AC) return null;
  if (!state.audioCtx || state.audioCtx.state === "closed") {
    state.audioCtx = new AC();
    state.playTime = 0;
  }
  if (state.audioCtx.state === "suspended") {
    try {
      await state.audioCtx.resume();
    } catch {
      /* iOS may still block until a later gesture */
    }
  }
  return state.audioCtx;
}

function floatToS16Frame(samples) {
  const pcm = new Int16Array(FRAME_SAMPLES);
  for (let i = 0; i < FRAME_SAMPLES; i++) {
    let s = samples[i] || 0;
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    pcm[i] = s < 0 ? Math.round(s * 32768) : Math.round(s * 32767);
  }
  return pcm.buffer;
}

function makeDownsampler(inRate, onFrame) {
  const step = inRate / PCM_RATE;
  let leftover = new Float32Array(0);
  const pending = [];
  return {
    push(chunk) {
      if (!chunk || !chunk.length) return;
      const merged = new Float32Array(leftover.length + chunk.length);
      merged.set(leftover);
      merged.set(chunk, leftover.length);
      const outCount = Math.max(0, Math.floor((merged.length - 1) / step));
      for (let n = 0; n < outCount; n++) {
        const src = n * step;
        const i0 = Math.floor(src);
        const frac = src - i0;
        const a = merged[i0] || 0;
        const b = merged[Math.min(i0 + 1, merged.length - 1)] || a;
        pending.push(a + (b - a) * frac);
        if (pending.length >= FRAME_SAMPLES) {
          onFrame(floatToS16Frame(pending.splice(0, FRAME_SAMPLES)));
        }
      }
      const consumed = Math.floor(outCount * step);
      leftover = merged.slice(consumed);
    },
    reset() {
      leftover = new Float32Array(0);
      pending.length = 0;
    },
  };
}

function encodeWav(int16, sampleRate) {
  const dataSize = int16.byteLength;
  const buf = new ArrayBuffer(44 + dataSize);
  const view = new DataView(buf);
  const write = (off, s) => {
    for (let i = 0; i < s.length; i++) view.setUint8(off + i, s.charCodeAt(i));
  };
  write(0, "RIFF");
  view.setUint32(4, 36 + dataSize, true);
  write(8, "WAVE");
  write(12, "fmt ");
  view.setUint32(16, 16, true);
  view.setUint16(20, 1, true);
  view.setUint16(22, 1, true);
  view.setUint32(24, sampleRate, true);
  view.setUint32(28, sampleRate * 2, true);
  view.setUint16(32, 2, true);
  view.setUint16(34, 16, true);
  write(36, "data");
  view.setUint32(40, dataSize, true);
  new Uint8Array(buf, 44).set(
    new Uint8Array(int16.buffer, int16.byteOffset, dataSize)
  );
  return new Blob([buf], { type: "audio/wav" });
}

function resampleTo16k(channel, inRate) {
  if (inRate === PCM_RATE) return channel;
  const outLen = Math.max(1, Math.round(channel.length * (PCM_RATE / inRate)));
  const out = new Float32Array(outLen);
  const step = inRate / PCM_RATE;
  for (let i = 0; i < outLen; i++) {
    const src = i * step;
    const j = Math.floor(src);
    const f = src - j;
    const a = channel[j] || 0;
    const b = channel[Math.min(j + 1, channel.length - 1)] || a;
    out[i] = a + (b - a) * f;
  }
  return out;
}

function floatsToInt16(channel) {
  const pcm = new Int16Array(channel.length);
  for (let i = 0; i < channel.length; i++) {
    let s = channel[i];
    if (s > 1) s = 1;
    if (s < -1) s = -1;
    pcm[i] = s < 0 ? Math.round(s * 32768) : Math.round(s * 32767);
  }
  return pcm;
}

async function blobToWav(blob) {
  const ctx = await ensureAudio();
  if (!ctx) return blob;
  const raw = await blob.arrayBuffer();
  let audio;
  try {
    audio = await ctx.decodeAudioData(raw.slice(0));
  } catch {
    return blob;
  }
  const mix = new Float32Array(audio.length);
  const channels = audio.numberOfChannels;
  for (let c = 0; c < channels; c++) {
    const data = audio.getChannelData(c);
    for (let i = 0; i < data.length; i++) mix[i] += data[i] / channels;
  }
  const resampled = resampleTo16k(mix, audio.sampleRate);
  return encodeWav(floatsToInt16(resampled), PCM_RATE);
}

function playIncoming(buffer) {
  if (state.holding || state.speakerMuted || !state.audioCtx) return;
  const ctx = state.audioCtx;
  let bytes = buffer;
  if (bytes.byteLength < 2) return;
  if (bytes.byteLength % 2) bytes = bytes.slice(0, bytes.byteLength - 1);
  const samples = new Int16Array(bytes);
  const f32 = new Float32Array(samples.length);
  for (let i = 0; i < samples.length; i++) f32[i] = samples[i] / 32768;
  const ratio = ctx.sampleRate / PCM_RATE;
  const outLen = Math.max(1, Math.floor(f32.length * ratio));
  const resampled = new Float32Array(outLen);
  for (let i = 0; i < outLen; i++) {
    const src = i / ratio;
    const j = Math.floor(src);
    const f = src - j;
    const a = f32[j] || 0;
    const b = f32[j + 1] ?? a;
    resampled[i] = a + (b - a) * f;
  }
  const buf = ctx.createBuffer(1, resampled.length, ctx.sampleRate);
  buf.copyToChannel(resampled, 0);
  const src = ctx.createBufferSource();
  src.buffer = buf;
  src.connect(ctx.destination);
  const now = ctx.currentTime;
  if (state.playTime < now + 0.02) state.playTime = now + 0.02;
  src.start(state.playTime);
  state.playTime += buf.duration;
}

const WORKLET_SRC = `
class FlCapture extends AudioWorkletProcessor {
  process(inputs) {
    const ch = inputs[0] && inputs[0][0];
    if (ch && ch.length) this.port.postMessage(ch.slice());
    return true;
  }
}
registerProcessor("fl-capture", FlCapture);
`;

let workletUrl = null;

async function ensureWorklet(ctx) {
  if (!ctx.audioWorklet) throw new Error("no worklet");
  if (!workletUrl) {
    workletUrl = URL.createObjectURL(
      new Blob([WORKLET_SRC], { type: "application/javascript" })
    );
  }
  await ctx.audioWorklet.addModule(workletUrl);
}

function sendPcm(frame) {
  const ws = state.ws;
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  if (!state.holding || state.hangout !== "live") return;
  if (state.floor && state.floor !== deviceId()) return;
  ws.send(frame);
}

async function startMicCapture() {
  if (state.capture) return;
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
    throw new Error("getUserMedia missing");
  }
  const ctx = await ensureAudio();
  if (!ctx) throw new Error("no AudioContext");
  const stream = await navigator.mediaDevices.getUserMedia({
    audio: {
      channelCount: 1,
      echoCancellation: true,
      noiseSuppression: true,
      autoGainControl: true,
    },
    video: false,
  });
  const source = ctx.createMediaStreamSource(stream);
  const down = makeDownsampler(ctx.sampleRate, sendPcm);
  const dead = ctx.createGain();
  dead.gain.value = 0;
  let node = null;
  let proc = null;
  try {
    await ensureWorklet(ctx);
    node = new AudioWorkletNode(ctx, "fl-capture");
    node.port.onmessage = (ev) => {
      if (state.holding) down.push(ev.data);
    };
    source.connect(node);
    node.connect(dead);
    dead.connect(ctx.destination);
  } catch {
    const makeProc = ctx.createScriptProcessor || ctx.createJavaScriptNode;
    if (!makeProc) {
      stream.getTracks().forEach((t) => t.stop());
      throw new Error("no audio capture node");
    }
    proc = makeProc.call(ctx, 2048, 1, 1);
    proc.onaudioprocess = (ev) => {
      if (!state.holding) return;
      down.push(ev.inputBuffer.getChannelData(0));
    };
    source.connect(proc);
    proc.connect(dead);
    dead.connect(ctx.destination);
  }
  state.mediaStream = stream;
  state.capture = {
    stop() {
      down.reset();
      try {
        source.disconnect();
      } catch {
        /* already gone */
      }
      try {
        if (node) node.disconnect();
      } catch {
        /* already gone */
      }
      try {
        if (proc) {
          proc.onaudioprocess = null;
          proc.disconnect();
        }
      } catch {
        /* already gone */
      }
      try {
        dead.disconnect();
      } catch {
        /* already gone */
      }
      stream.getTracks().forEach((t) => t.stop());
      if (state.mediaStream === stream) state.mediaStream = null;
    },
  };
}

function stopMicCapture() {
  if (state.capture) {
    state.capture.stop();
    state.capture = null;
  }
}

function wsSend(obj) {
  const ws = state.ws;
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  ws.send(JSON.stringify(obj));
}

function applyMe(me) {
  state.me = me || {};
  ui.peer.textContent = state.me.peer_id || "—";
  if (typeof state.me.peer_online === "boolean") setOnline(state.me.peer_online);
  ui.unread.textContent =
    state.me.unread === undefined || state.me.unread === null
      ? "—"
      : String(state.me.unread);
  ui.playhead.textContent =
    state.me.playhead === undefined || state.me.playhead === null
      ? "—"
      : String(state.me.playhead);
  updateChrome();
}

async function fetchMe() {
  const me = await api("/v1/me");
  applyMe(me);
  return me;
}

async function beat() {
  try {
    const body = await api("/v1/heartbeat", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ uptime_s: Math.round(performance.now() / 1000) }),
    });
    if (body && typeof body.peer_online === "boolean") setOnline(body.peer_online);
  } catch (err) {
    if (err.status === 401) {
      setLine(ui.statusLine, "Token rejected.");
      await disconnect();
      return;
    }
    setLine(ui.statusLine, "Heartbeat failed: " + err.message);
  }
}

function parseMessages(data) {
  if (Array.isArray(data)) return data;
  if (data && Array.isArray(data.messages)) return data.messages;
  return [];
}

function revokeUrls() {
  for (const url of state.objectUrls) URL.revokeObjectURL(url);
  state.objectUrls = [];
}

async function fetchBlob(seq, mime) {
  const res = await fetch(httpBase() + "/v1/messages/" + seq + "/blob", {
    headers: authHeaders(),
  });
  if (!res.ok) throw new Error("blob " + res.status);
  const buf = await res.arrayBuffer();
  const blob = new Blob([buf], { type: mime });
  const url = URL.createObjectURL(blob);
  state.objectUrls.push(url);
  return url;
}

async function refreshInbox() {
  if (!state.connected) return;
  let data;
  try {
    data = await api("/v1/messages?after=0");
  } catch (err) {
    try {
      data = await api("/v1/messages");
    } catch (err2) {
      setLine(ui.statusLine, "Inbox failed: " + err2.message);
      return;
    }
  }
  const items = parseMessages(data).slice().sort((a, b) => (b.seq || 0) - (a.seq || 0));
  revokeUrls();
  ui.inbox.replaceChildren();
  if (!items.length) {
    const empty = document.createElement("li");
    empty.className = "empty";
    empty.textContent = "Nothing from the box yet.";
    ui.inbox.appendChild(empty);
    return;
  }
  for (const msg of items) {
    ui.inbox.appendChild(renderMessage(msg));
  }
}

function renderMessage(msg) {
  const li = document.createElement("li");
  const meta = document.createElement("div");
  meta.className = "meta";
  const seq = document.createElement("span");
  seq.textContent = "#" + (msg.seq ?? "?");
  const kind = document.createElement("span");
  kind.textContent = msg.kind || "message";
  const from = document.createElement("span");
  from.textContent = msg.from ? "from " + msg.from : "";
  meta.append(seq, kind, from);
  li.appendChild(meta);

  if (msg.kind === "text") {
    const body = document.createElement("div");
    body.className = "body";
    body.textContent = msg.text || "";
    li.appendChild(body);
  } else if (msg.kind === "audio") {
    const audio = document.createElement("audio");
    audio.controls = true;
    audio.preload = "none";
    audio.setAttribute("aria-label", "Play voicemail " + (msg.seq || ""));
    fetchBlob(msg.seq, "audio/wav")
      .then((url) => {
        audio.src = url;
      })
      .catch(() => {
        audio.replaceWith(document.createTextNode("Could not load audio."));
      });
    li.appendChild(audio);
  } else if (msg.kind === "image") {
    const img = document.createElement("img");
    img.alt = "Photo from the box";
    fetchBlob(msg.seq, "image/jpeg")
      .then((url) => {
        img.src = url;
      })
      .catch(() => {
        img.replaceWith(document.createTextNode("Could not load photo."));
      });
    li.appendChild(img);
  } else if (msg.text) {
    const body = document.createElement("div");
    body.className = "body";
    body.textContent = msg.text;
    li.appendChild(body);
  }
  return li;
}

function onWsMessage(ev) {
  if (typeof ev.data !== "string") {
    if (ev.data instanceof ArrayBuffer) {
      playIncoming(ev.data);
    } else if (ev.data && typeof ev.data.arrayBuffer === "function") {
      ev.data.arrayBuffer().then(playIncoming);
    }
    return;
  }
  let msg;
  try {
    msg = JSON.parse(ev.data);
  } catch {
    return;
  }
  const type = msg.type;
  if (type === "hello_ok") {
    if (msg.peer_id) ui.peer.textContent = msg.peer_id;
    if (msg.playhead !== undefined) ui.playhead.textContent = String(msg.playhead);
    setLine(ui.statusLine, "Connected. Waiting on the box.");
    refreshInbox();
  } else if (type === "inbox") {
    setLine(ui.statusLine, "New " + (msg.kind || "mail") + " from " + (msg.from || "box"));
    refreshInbox();
    fetchMe().catch(() => {});
  } else if (type === "presence") {
    if (typeof msg.online === "boolean") setOnline(msg.online);
  } else if (type === "ring") {
    state.hangout = "ringing";
    setLine(ui.hangoutLine, hangoutLabel());
    updateChrome();
  } else if (type === "session_start") {
    state.hangout = "live";
    state.floor = null;
    setLine(ui.hangoutLine, hangoutLabel());
    updateChrome();
  } else if (type === "floor") {
    state.floor = msg.holder || null;
    if (state.holding && !state.floor) wsSend({ type: "floor_request" });
    setLine(ui.hangoutLine, hangoutLabel());
  } else if (type === "floor_denied") {
    setLine(ui.hangoutLine, "Floor denied — wait your turn.");
  } else if (type === "hangup" || type === "timeout") {
    endHangoutLocal(type === "timeout" ? "Invite timed out." : "Hangout ended.");
  }
}

function endHangoutLocal(reason) {
  stopTalk();
  state.hangout = "idle";
  state.floor = null;
  setLine(ui.hangoutLine, reason || hangoutLabel());
  updateChrome();
}

function openSocket() {
  if (!state.wsWanted) return;
  if (state.ws && (state.ws.readyState === WebSocket.OPEN || state.ws.readyState === WebSocket.CONNECTING)) {
    return;
  }
  let socket;
  try {
    socket = new WebSocket(wsUrl());
  } catch (err) {
    setLine(ui.statusLine, "WebSocket failed: " + err.message);
    return;
  }
  state.ws = socket;
  socket.binaryType = "arraybuffer";
  socket.onopen = () => {
    wsSend({ type: "hello", device_id: deviceId(), token: token() });
  };
  socket.onmessage = onWsMessage;
  socket.onerror = () => {
    setLine(ui.statusLine, "WebSocket error.");
  };
  socket.onclose = () => {
    if (state.ws === socket) state.ws = null;
    if (state.hangout !== "idle") endHangoutLocal("Socket closed.");
    if (state.wsWanted) {
      setTimeout(openSocket, 2000);
    }
  };
}

async function connect() {
  saveSettings();
  setMicNote(secureMicHint());
  await ensureAudio();
  try {
    await fetchMe();
  } catch (err) {
    setLine(ui.statusLine, "Connect failed: " + err.message);
    return;
  }
  state.connected = true;
  state.wsWanted = true;
  setLine(ui.statusLine, "HTTP ok. Opening socket…");
  updateChrome();
  await beat();
  if (state.heartbeat) clearInterval(state.heartbeat);
  state.heartbeat = setInterval(beat, HEARTBEAT_MS);
  openSocket();
  await refreshInbox();
}

async function disconnect() {
  state.wsWanted = false;
  state.connected = false;
  if (state.heartbeat) {
    clearInterval(state.heartbeat);
    state.heartbeat = null;
  }
  stopTalk();
  stopRecording(false);
  if (state.hangout !== "idle") wsSend({ type: "hangup" });
  state.hangout = "idle";
  state.floor = null;
  if (state.ws) {
    try {
      state.ws.close();
    } catch {
      /* ignore */
    }
    state.ws = null;
  }
  stopMicCapture();
  setOnline(false);
  ui.peer.textContent = "—";
  ui.unread.textContent = "—";
  ui.playhead.textContent = "—";
  setLine(ui.statusLine, "Disconnected.");
  setLine(ui.hangoutLine, hangoutLabel());
  updateChrome();
}

async function startTalk() {
  if (!state.connected || state.hangout !== "live" || state.holding) return;
  if (state.recorder) stopRecording(false);
  state.holding = true;
  state.speakerMuted = true;
  state.playTime = 0;
  ui.ptt.setAttribute("aria-pressed", "true");
  ui.pttState.textContent = "talking";
  setLine(ui.hangoutLine, hangoutLabel());
  wsSend({ type: "floor_request" });
  try {
    await startMicCapture();
    if (!state.holding) {
      stopMicCapture();
      return;
    }
  } catch (err) {
    state.holding = false;
    state.speakerMuted = false;
    ui.ptt.setAttribute("aria-pressed", "false");
    ui.pttState.textContent = "mic unavailable";
    wsSend({ type: "floor_release" });
    setMicNote(
      (secureMicHint() ||
        "Could not open the microphone (" +
          err.message +
          "). Need HTTPS (or localhost on this Mac).") +
        " Text, photo, and a WAV file still work."
    );
    setLine(ui.hangoutLine, hangoutLabel());
  }
}

function stopTalk() {
  const wasHolding = state.holding;
  state.holding = false;
  state.speakerMuted = false;
  ui.ptt.setAttribute("aria-pressed", "false");
  ui.pttState.textContent = "release = listen";
  stopMicCapture();
  if (wasHolding) wsSend({ type: "floor_release" });
  if (state.hangout === "live") setLine(ui.hangoutLine, hangoutLabel());
}

function pickRecorderMime() {
  if (typeof MediaRecorder === "undefined") return "";
  const types = [
    "audio/webm;codecs=opus",
    "audio/webm",
    "audio/mp4",
    "audio/aac",
    "audio/wav",
  ];
  for (const t of types) {
    if (MediaRecorder.isTypeSupported && MediaRecorder.isTypeSupported(t)) return t;
  }
  return "";
}

async function toggleRecord() {
  if (state.recorder) {
    stopRecording(true);
    return;
  }
  if (!state.connected) return;
  if (state.holding) {
    setLine(ui.composeLine, "Release hold-to-talk before recording a voicemail.");
    return;
  }
  if (typeof MediaRecorder === "undefined" || !navigator.mediaDevices) {
    setLine(ui.composeLine, "No MediaRecorder here — use a WAV file instead.");
    ui.wavFile.click();
    return;
  }
  try {
    await ensureAudio();
    const stream = await navigator.mediaDevices.getUserMedia({ audio: true });
    state.recordStream = stream;
    const mime = pickRecorderMime();
    const recorder = mime ? new MediaRecorder(stream, { mimeType: mime }) : new MediaRecorder(stream);
    state.recordChunks = [];
    recorder.ondataavailable = (ev) => {
      if (ev.data && ev.data.size) state.recordChunks.push(ev.data);
    };
    recorder.onerror = () => {
      setLine(ui.composeLine, "Recorder error.");
    };
    recorder.onstop = async () => {
      stream.getTracks().forEach((t) => t.stop());
      if (state.recordStream === stream) state.recordStream = null;
      const type = recorder.mimeType || mime || "audio/webm";
      const blob = new Blob(state.recordChunks, { type });
      state.recordChunks = [];
      if (blob.size) {
        let wav = blob;
        try {
          wav = await blobToWav(blob);
        } catch {
          wav = blob;
        }
        await postBlob("audio", wav, wav.type === "audio/wav" ? "voicemail.wav" : "voicemail");
      }
    };
    state.recorder = recorder;
    state.recordStarted = Date.now();
    recorder.start();
    ui.record.textContent = "Stop & send";
    setLine(ui.composeLine, "Recording voicemail…");
    state.recordTimer = setInterval(() => {
      const s = Math.round((Date.now() - state.recordStarted) / 1000);
      setLine(ui.composeLine, "Recording voicemail… " + s + "s");
      if (s >= VOICEMAIL_MAX_S) stopRecording(true);
    }, 250);
  } catch (err) {
    setMicNote(
      (secureMicHint() || "Microphone failed: " + err.message) +
        " You can still send a WAV file, a photo, or text."
    );
    setLine(ui.composeLine, "Use a WAV file if the mic is blocked.");
  }
}

function stopRecording(send) {
  if (state.recordTimer) {
    clearInterval(state.recordTimer);
    state.recordTimer = null;
  }
  ui.record.textContent = "Record voicemail";
  const rec = state.recorder;
  state.recorder = null;
  if (!rec) return;
  try {
    if (rec.state !== "inactive") rec.stop();
  } catch {
    /* already stopped */
  }
  if (!send) {
    rec.onstop = null;
    if (state.recordStream) {
      state.recordStream.getTracks().forEach((t) => t.stop());
      state.recordStream = null;
    }
  }
}

async function postBlob(kind, blob, filename) {
  const fd = new FormData();
  fd.append("kind", kind);
  fd.append("blob", blob, filename);
  try {
    await api("/v1/messages", { method: "POST", body: fd });
    setLine(ui.composeLine, kind === "audio" ? "Voicemail sent to the box." : "Photo sent to the box.");
  } catch (err) {
    setLine(ui.composeLine, "Send failed: " + err.message);
  }
}

async function sendText(ev) {
  ev.preventDefault();
  if (!state.connected) return;
  const text = ui.noteText.value.trim();
  if (!text) return;
  const fd = new FormData();
  fd.append("kind", "text");
  fd.append("text", text);
  try {
    await api("/v1/messages", { method: "POST", body: fd });
    ui.noteText.value = "";
    setLine(ui.composeLine, "Text sent to the box.");
  } catch (err) {
    setLine(ui.composeLine, "Send failed: " + err.message);
  }
}

function bindPtt() {
  const ptt = ui.ptt;
  ptt.addEventListener("pointerdown", (ev) => {
    if (ptt.disabled) return;
    if (ev.pointerType === "mouse" && ev.button !== 0) return;
    ev.preventDefault();
    state.pttPointer = ev.pointerId;
    try {
      ptt.setPointerCapture(ev.pointerId);
    } catch {
      /* some browsers refuse capture */
    }
    startTalk();
  });
  const end = (ev) => {
    if (state.pttPointer != null && ev.pointerId !== state.pttPointer) return;
    state.pttPointer = null;
    stopTalk();
  };
  ptt.addEventListener("pointerup", end);
  ptt.addEventListener("pointercancel", end);
  ptt.addEventListener("lostpointercapture", () => {
    if (state.holding) stopTalk();
  });
  ptt.addEventListener("contextmenu", (ev) => ev.preventDefault());
  ptt.addEventListener("keydown", (ev) => {
    if (ev.code !== "Space" && ev.code !== "Enter") return;
    ev.preventDefault();
    if (!state.holding) startTalk();
  });
  ptt.addEventListener("keyup", (ev) => {
    if (ev.code !== "Space" && ev.code !== "Enter") return;
    ev.preventDefault();
    stopTalk();
  });
  window.addEventListener("blur", () => {
    if (state.holding) stopTalk();
  });
}

function init() {
  loadSettings();
  setMicNote(secureMicHint());
  updateChrome();
  bindPtt();

  ui.settings.addEventListener("submit", (ev) => {
    ev.preventDefault();
    if (state.connected) disconnect();
    else connect();
  });
  ui.origin.addEventListener("change", saveSettings);
  ui.deviceId.addEventListener("change", saveSettings);
  ui.token.addEventListener("change", saveSettings);

  ui.invite.addEventListener("click", async () => {
    await ensureAudio();
    wsSend({ type: "invite" });
    state.hangout = "calling";
    setLine(ui.hangoutLine, hangoutLabel());
    updateChrome();
  });
  ui.accept.addEventListener("click", async () => {
    await ensureAudio();
    wsSend({ type: "accept" });
  });
  ui.hangup.addEventListener("click", () => {
    wsSend({ type: "hangup" });
    endHangoutLocal("You hung up.");
  });

  ui.textForm.addEventListener("submit", sendText);
  ui.record.addEventListener("click", toggleRecord);
  ui.refresh.addEventListener("click", refreshInbox);

  ui.wavFile.addEventListener("change", async () => {
    const file = ui.wavFile.files && ui.wavFile.files[0];
    ui.wavFile.value = "";
    if (!file || !state.connected) return;
    await postBlob("audio", file, file.name || "clip.wav");
  });
  ui.photoFile.addEventListener("change", async () => {
    const file = ui.photoFile.files && ui.photoFile.files[0];
    ui.photoFile.value = "";
    if (!file || !state.connected) return;
    await postBlob("image", file, file.name || "photo.jpg");
  });
}

init();
