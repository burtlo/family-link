/* BOX-3 twin for h18: same GET /demo/h18/message catalog as the kit. */
(() => {
  const ROOMVOL_FIRST_ON = 78;
  const ROOMVOL_STEP = 2;
  const ROOMVOL_MAX = 100;
  const ROOMVOL_SLIDER_MAX =
    ((ROOMVOL_MAX - ROOMVOL_FIRST_ON) / ROOMVOL_STEP) + 1;

  const lcd = document.getElementById("lcd");
  const statusEl = document.getElementById("status");
  const senderEl = document.getElementById("sender");
  const whenEl = document.getElementById("when");
  const badgeEl = document.getElementById("badge");
  const idxEl = document.getElementById("idx");
  const barEl = document.getElementById("bar");
  const barFill = document.getElementById("bar-fill");
  const clockEl = document.getElementById("clock");
  const playEl = document.getElementById("play");
  const volLab = document.getElementById("vol-lab");
  const sliderEl = document.getElementById("slider");
  const sliderFill = document.getElementById("slider-fill");
  const sliderKnob = document.getElementById("slider-knob");
  const bootEl = document.getElementById("boot");
  const resetEl = document.getElementById("reset");
  const muteEl = document.getElementById("mute");
  const circleEl = document.getElementById("circle");
  const lineEl = document.getElementById("line");

  const audio = new Audio();
  audio.preload = "auto";

  const state = {
    msg: null,
    notch: 0,
    playing: false,
    circleHeld: false,
  };

  function roomvolCodec(notch) {
    if (notch <= 0) {
      return 0;
    }
    const v = ROOMVOL_FIRST_ON + (notch - 1) * ROOMVOL_STEP;
    return v > ROOMVOL_MAX ? ROOMVOL_MAX : v;
  }

  function webGain(notch) {
    if (notch <= 0) {
      return 0;
    }
    return 0.35 + 0.65 * (notch / ROOMVOL_SLIDER_MAX);
  }

  function fmtClock(posMs, durMs) {
    const p = Math.max(0, Math.floor(posMs / 1000));
    const d = Math.max(0, Math.floor(durMs / 1000));
    return `${Math.floor(p / 60)}:${String(p % 60).padStart(2, "0")} / ${Math.floor(d / 60)}:${String(d % 60).padStart(2, "0")}`;
  }

  function fmtLevel(codec) {
    return codec <= 0 ? "mute" : String(codec);
  }

  function positionMs() {
    if (!state.msg) {
      return 0;
    }
    if (state.playing || !audio.paused) {
      return Math.round(audio.currentTime * 1000);
    }
    return state.msg.position_ms || 0;
  }

  function paintVolume() {
    const codec = roomvolCodec(state.notch);
    const t = state.notch / ROOMVOL_SLIDER_MAX;
    sliderFill.style.width = `${t * 100}%`;
    sliderKnob.style.left = `${t * 100}%`;
    volLab.textContent = fmtLevel(codec);
    sliderEl.setAttribute("aria-valuenow", String(state.notch));
    audio.volume = webGain(state.notch);
    audio.muted = state.notch <= 0;
  }

  function paint() {
    const msg = state.msg;
    if (!msg) {
      return;
    }
    lcd.classList.add("ready");
    senderEl.textContent = msg.sender || "(sender)";
    whenEl.textContent = msg.sent_at || "";
    badgeEl.textContent = msg.read ? "read" : "unread";
    badgeEl.className = `badge ${msg.read ? "read" : "unread"}`;
    const count = msg.count > 0 ? msg.count : 1;
    idxEl.textContent = `${(msg.index || 0) + 1}/${count}`;
    const dur = msg.duration_ms || 1;
    const pos = Math.min(positionMs(), dur);
    const frac = Math.max(0, Math.min(1, pos / dur));
    barFill.style.width = `${frac * 100}%`;
    barEl.setAttribute("aria-valuemax", String(dur));
    barEl.setAttribute("aria-valuenow", String(pos));
    clockEl.textContent = fmtClock(pos, dur);
    playEl.textContent = state.playing ? "Pause" : "Play";
    paintVolume();
  }

  function audioSrc(msg) {
    try {
      const url = new URL(msg.url, location.origin);
      return url.pathname + url.search;
    } catch {
      return msg.url;
    }
  }

  async function loadMessage(index) {
    audio.pause();
    state.playing = false;
    lineEl.textContent = `GET /demo/h18/message?i=${index}`;
    const res = await fetch(`/demo/h18/message?i=${index}`);
    if (!res.ok) {
      lcd.classList.remove("ready");
      statusEl.textContent = "no wifi";
      lineEl.textContent = `message fetch failed (${res.status})`;
      throw new Error(`message ${res.status}`);
    }
    const msg = await res.json();
    msg.read = Boolean(msg.read);
    state.msg = msg;
    audio.src = audioSrc(msg);
    await new Promise((resolve, reject) => {
      const onOk = () => {
        audio.removeEventListener("error", onErr);
        resolve();
      };
      const onErr = () => {
        audio.removeEventListener("loadedmetadata", onOk);
        reject(new Error("audio failed"));
      };
      audio.addEventListener("loadedmetadata", onOk, { once: true });
      audio.addEventListener("error", onErr, { once: true });
    });
    const start = Math.max(0, (msg.position_ms || 0) / 1000);
    try {
      audio.currentTime = start;
    } catch {
      /* some browsers reject a seek until more data arrives */
    }
    paint();
    lineEl.textContent = `${msg.sender} · ${msg.id} · ${msg.index + 1}/${msg.count}`;
  }

  function applyNotch(n) {
    state.notch = Math.max(0, Math.min(ROOMVOL_SLIDER_MAX, Math.round(n)));
    paintVolume();
  }

  async function togglePlay() {
    if (!state.msg) {
      return;
    }
    if (state.playing) {
      audio.pause();
      state.playing = false;
      state.msg.position_ms = positionMs();
      paint();
      return;
    }
    if (state.msg.duration_ms > 0 && positionMs() >= state.msg.duration_ms) {
      state.msg.position_ms = 0;
      audio.currentTime = 0;
    }
    state.msg.read = true;
    try {
      await audio.play();
      state.playing = true;
    } catch (err) {
      lineEl.textContent = `play failed: ${err.message}`;
      state.playing = false;
    }
    paint();
  }

  async function nextMessage() {
    if (!state.msg) {
      return;
    }
    const count = state.msg.count > 0 ? state.msg.count : 1;
    const next = ((state.msg.index || 0) + 1) % count;
    bootEl.classList.add("down");
    window.setTimeout(() => bootEl.classList.remove("down"), 120);
    try {
      await loadMessage(next);
    } catch (err) {
      lineEl.textContent = `next failed: ${err.message}`;
    }
  }

  function bindDrag(el, onFrac) {
    const go = (ev) => {
      const r = el.getBoundingClientRect();
      const x = r.width ? (ev.clientX - r.left) / r.width : 0;
      onFrac(Math.max(0, Math.min(1, x)));
    };
    el.addEventListener("pointerdown", (ev) => {
      ev.preventDefault();
      el.setPointerCapture(ev.pointerId);
      go(ev);
    });
    el.addEventListener("pointermove", (ev) => {
      if (el.hasPointerCapture(ev.pointerId)) {
        go(ev);
      }
    });
  }

  function setCircle(held) {
    state.circleHeld = held;
    circleEl.setAttribute("aria-pressed", held ? "true" : "false");
  }

  function setMute(down) {
    muteEl.setAttribute("aria-pressed", down ? "true" : "false");
  }

  function setScale(n) {
    const scale = Number(n) || 2;
    document.documentElement.style.setProperty("--scale", String(scale));
    document.querySelectorAll(".scale-btn").forEach((btn) => {
      btn.classList.toggle("on", Number(btn.dataset.scale) === scale);
    });
    try {
      localStorage.setItem("h18-box-scale", String(scale));
    } catch {
      /* ignore */
    }
  }

  playEl.addEventListener("click", () => {
    togglePlay();
  });
  bootEl.addEventListener("pointerdown", (ev) => {
    ev.preventDefault();
    nextMessage();
  });
  resetEl.addEventListener("click", () => {
    location.reload();
  });
  muteEl.addEventListener("click", () => {
    setMute(muteEl.getAttribute("aria-pressed") !== "true");
  });
  circleEl.addEventListener("pointerdown", (ev) => {
    ev.preventDefault();
    circleEl.setPointerCapture(ev.pointerId);
    setCircle(true);
  });
  circleEl.addEventListener("pointerup", () => setCircle(false));
  circleEl.addEventListener("pointercancel", () => setCircle(false));

  bindDrag(barEl, (frac) => {
    if (!state.msg || !state.msg.duration_ms) {
      return;
    }
    const ms = Math.round(frac * state.msg.duration_ms);
    state.msg.position_ms = ms;
    try {
      audio.currentTime = ms / 1000;
    } catch {
      /* ignore */
    }
    paint();
  });
  bindDrag(sliderEl, (frac) => {
    applyNotch(frac * ROOMVOL_SLIDER_MAX);
  });

  audio.addEventListener("timeupdate", () => {
    if (state.msg) {
      state.msg.position_ms = positionMs();
    }
    paint();
  });
  audio.addEventListener("ended", () => {
    state.playing = false;
    if (state.msg) {
      state.msg.position_ms = state.msg.duration_ms;
    }
    paint();
  });
  audio.addEventListener("pause", () => {
    if (audio.ended) {
      return;
    }
    state.playing = false;
    paint();
  });

  window.addEventListener("keydown", (ev) => {
    if (ev.repeat) {
      return;
    }
    const k = ev.key;
    if (k === "b" || k === "B" || k === "n" || k === "N" || k === "ArrowRight") {
      ev.preventDefault();
      nextMessage();
      return;
    }
    if (k === "m" || k === "M") {
      ev.preventDefault();
      setMute(muteEl.getAttribute("aria-pressed") !== "true");
      return;
    }
    if (k === "c" || k === "C" || k === " ") {
      ev.preventDefault();
      setCircle(true);
    }
  });
  window.addEventListener("keyup", (ev) => {
    if (ev.key === "c" || ev.key === "C" || ev.key === " ") {
      setCircle(false);
    }
  });

  document.querySelectorAll(".scale-btn").forEach((btn) => {
    btn.addEventListener("click", () => setScale(btn.dataset.scale));
  });
  try {
    const saved = localStorage.getItem("h18-box-scale");
    if (saved) {
      setScale(saved);
    }
  } catch {
    /* ignore */
  }

  loadMessage(0).catch((err) => {
    lcd.classList.remove("ready");
    statusEl.textContent = "no wifi";
    lineEl.textContent = err.message;
  });
})();
