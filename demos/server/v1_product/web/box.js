/* v1 product BOX-3 twin — carousel, shoulder settings, profile API */
(() => {
  const ROOMVOL_FIRST = 78;
  const ROOMVOL_STEP = 2;
  const ROOMVOL_MAX = 100;
  const ROOMVOL_ON = ((ROOMVOL_MAX - ROOMVOL_FIRST) / ROOMVOL_STEP) + 1;
  const CIRCLE_DEBOUNCE_MS = 400;

  const ACCENTS = [
    "#5AA0E8", "#E8C040", "#7AC47A", "#C070E8", "#E87A9A",
    "#7AD4E8", "#D4A0E8", "#E8A87A", "#4ECDC4", "#FF6B6B",
  ];

  const DEFAULT_ACCENTS = ACCENTS;

  const $ = (id) => document.getElementById(id);

  const lcd = $("lcd");
  const countEl = $("count");
  const ribbonToast = $("ribbon-toast");
  const backHint = $("back-hint");
  const screenCarousel = $("screen-carousel");
  const screenSettings = $("screen-settings");
  const screenPicker = $("screen-picker");
  const peekLeft = $("peek-left");
  const peekRight = $("peek-right");
  const msgCard = $("msg-card");
  const facePane = $("face-pane");
  const playPane = $("play-pane");
  const playBtn = $("play-btn");
  const playIcon = $("play-icon");
  const scrubEl = $("scrub");
  const pickerList = $("picker-list");
  const settingsScroll = $("settings-scroll");
  const volSlider = $("vol-slider");
  const volNum = $("vol-num");
  const swatchGrid = $("swatch-grid");
  const faceGrid = $("face-grid");
  const settingsName = $("settings-name");
  const setupPanel = $("setup");
  const setupStatus = $("setup-status");
  const lineEl = $("line");
  const bootEl = $("boot");
  const circleEl = $("circle");
  const muteEl = $("mute");

  const player = new Audio();
  player.preload = "auto";

  const state = {
    mode: "login",
    origin: "",
    token: "",
    sessionUser: "",
    hangoutUsers: [],
    inbox: [],
    focus: 0,
    volNotch: 4,
    ws: null,
    playing: false,
    scrubbing: false,
    carouselReadyAt: 0,
    sendHintShown: false,
    profile: { avatar_slot: 0, accent_hex: ACCENTS[0] },
  };

  function roomvolCodec(notch) {
    if (notch <= 0) return 0;
    const v = ROOMVOL_FIRST + (notch - 1) * ROOMVOL_STEP;
    return v > ROOMVOL_MAX ? ROOMVOL_MAX : v;
  }

  function authHeaders() {
    return { Authorization: "Bearer " + state.token };
  }

  function sessionHeaders() {
    return { ...authHeaders(), "X-User-Id": state.sessionUser };
  }

  function defaultAccentForUser(userId) {
    const ids = state.hangoutUsers.map((u) => u.id);
    const idx = Math.max(0, ids.indexOf(userId));
    return DEFAULT_ACCENTS[idx % DEFAULT_ACCENTS.length];
  }

  function profileFor(userId) {
    const u = state.hangoutUsers.find((x) => x.id === userId);
    if (!u || !u.profile) {
      return {
        avatar_slot: 0,
        accent_hex: defaultAccentForUser(userId),
      };
    }
    return {
      avatar_slot: u.profile.avatar_slot || 0,
      accent_hex: u.profile.accent_hex || defaultAccentForUser(userId),
    };
  }

  function senderKey(msg) {
    return msg.from_label || msg.from || "Family";
  }

  function userIdForLabel(label) {
    const u = state.hangoutUsers.find(
      (x) => x.name === label || x.id === label,
    );
    return u ? u.id : label;
  }

  function lighten(hex, pct) {
    const n = parseInt(hex.slice(1), 16);
    let r = (n >> 16) & 0xff;
    let g = (n >> 8) & 0xff;
    let b = n & 0xff;
    r = Math.round(r + (255 - r) * pct / 100);
    g = Math.round(g + (255 - g) * pct / 100);
    b = Math.round(b + (255 - b) * pct / 100);
    return (
      "#" +
      [r, g, b]
        .map((c) => c.toString(16).padStart(2, "0"))
        .join("")
        .toUpperCase()
    );
  }

  function portraitEl(userId, sizePx) {
    const prof = profileFor(userId);
    const wrap = document.createElement("div");
    wrap.className = "portrait";
    wrap.style.width = sizePx + "px";
    wrap.style.height = sizePx + "px";
    const slot = prof.avatar_slot;
    if (slot >= 1 && slot <= 12) {
      const img = document.createElement("img");
      img.src = "avatars/avatar-" + slot + ".png";
      img.alt = "";
      img.onerror = () => {
        img.remove();
        wrap.classList.add("geometry");
        wrap.style.background = prof.accent_hex;
        wrap.innerHTML =
          '<span class="eye l"></span><span class="eye r"></span>';
      };
      wrap.appendChild(img);
    } else {
      wrap.classList.add("geometry");
      wrap.style.background = prof.accent_hex;
      wrap.innerHTML = '<span class="eye l"></span><span class="eye r"></span>';
    }
    return wrap;
  }

  function setToast(msg, ms = 2500) {
    ribbonToast.textContent = msg || "";
    if (msg) {
      backHint.style.display = "none";
      if (ms > 0) {
        setTimeout(() => {
          if (ribbonToast.textContent === msg) {
            ribbonToast.textContent = "";
            backHint.style.display = "";
          }
        }, ms);
      }
    } else {
      backHint.style.display = "";
    }
  }

  function setMode(mode) {
    state.mode = mode;
    const overlay = $("status-overlay");
    screenCarousel.classList.toggle("hidden", mode !== "carousel");
    screenSettings.classList.toggle("hidden", mode !== "settings");
    screenPicker.classList.toggle("hidden", mode !== "picker");

    if (mode === "login") {
      overlay.style.display = "flex";
      overlay.textContent = "sign in";
      screenCarousel.classList.add("hidden");
      screenSettings.classList.add("hidden");
      screenPicker.classList.add("hidden");
      return;
    }
    overlay.style.display = "none";

    if (mode === "carousel") {
      state.carouselReadyAt = Date.now();
      backHint.style.display = "none";
      backHint.textContent = "";
    } else if (mode === "settings" || mode === "picker") {
      backHint.style.display = "";
      backHint.textContent = "shoulder = back";
    }
    paint();
  }

  function currentMsg() {
    return state.inbox[state.focus] || null;
  }

  function paintPeek(btn, msg, side) {
    btn.replaceChildren();
    if (!msg) {
      btn.classList.add("hidden");
      return;
    }
    btn.classList.remove("hidden");
    btn.classList.toggle("unread-left", !msg.read && side === "left");
    btn.classList.toggle("unread-right", !msg.read && side === "right");
    const uid = userIdForLabel(senderKey(msg));
    const prof = profileFor(uid);
    const band = document.createElement("div");
    band.className = "peek-band";
    band.style.background = prof.accent_hex;
    band.appendChild(portraitEl(uid, 32));
    btn.appendChild(band);
    const name = document.createElement("div");
    name.className = "peek-name";
    name.textContent = (senderKey(msg) || "?").slice(0, 6);
    btn.appendChild(name);
  }

  function paintCarousel() {
    const m = currentMsg();
    const n = state.inbox.length;
    countEl.textContent = n > 0 ? `${state.focus + 1}/${n}` : "0/0";

    paintPeek(peekLeft, state.focus > 0 ? state.inbox[state.focus - 1] : null, "left");
    paintPeek(
      peekRight,
      state.focus + 1 < n ? state.inbox[state.focus + 1] : null,
      "right",
    );

    if (!m) {
      msgCard.classList.remove("unread");
      facePane.style.background = "#2a3038";
      playPane.style.background = "#2a3038";
      facePane.replaceChildren();
      facePane.textContent = "empty";
      scrubEl.value = 0;
      return;
    }

    const uid = userIdForLabel(senderKey(m));
    const prof = profileFor(uid);
    msgCard.classList.toggle("unread", !m.read);
    facePane.style.background = prof.accent_hex;
    playPane.style.background = lighten(prof.accent_hex, 28);
    facePane.replaceChildren();
    facePane.appendChild(portraitEl(uid, 96));

    const dur = m.duration_ms || 1;
    const pos = state.playing
      ? Math.round(player.currentTime * 1000)
      : m.position_ms || 0;
    scrubEl.max = dur;
    scrubEl.value = pos;
    playIcon.className =
      "play-icon " + (state.playing && !player.paused ? "pause" : "play");
  }

  function paintSettings() {
    settingsName.textContent =
      state.hangoutUsers.find((u) => u.id === state.sessionUser)?.name ||
      state.sessionUser;
    volSlider.max = ROOMVOL_ON;
    volSlider.value = state.volNotch;
    volNum.textContent = String(roomvolCodec(state.volNotch));

    swatchGrid.replaceChildren();
    const myAccent = state.profile.accent_hex;
    ACCENTS.forEach((hex) => {
      const b = document.createElement("button");
      b.type = "button";
      b.className = "swatch" + (hex.toUpperCase() === myAccent.toUpperCase() ? " on" : "");
      b.style.background = hex;
      b.addEventListener("click", () => pickAccent(hex));
      swatchGrid.appendChild(b);
    });

    faceGrid.replaceChildren();
    for (let slot = 0; slot <= 12; slot++) {
      const b = document.createElement("button");
      b.type = "button";
      b.className =
        "face-btn" + (slot === state.profile.avatar_slot ? " on" : "");
      if (slot === 0) {
        const p = portraitEl(state.sessionUser, 40);
        b.appendChild(p);
      } else {
        const p = document.createElement("div");
        p.className = "portrait";
        p.style.width = "40px";
        p.style.height = "40px";
        const img = document.createElement("img");
        img.src = "avatars/avatar-" + slot + ".png";
        img.alt = "";
        img.onerror = () => {
          p.classList.add("geometry");
          p.style.background = ACCENTS[(slot - 1) % ACCENTS.length];
          p.innerHTML = '<span class="eye l"></span><span class="eye r"></span>';
        };
        p.appendChild(img);
        b.appendChild(p);
      }
      b.addEventListener("click", () => pickAvatar(slot));
      faceGrid.appendChild(b);
    }
  }

  function paintPicker() {
    pickerList.replaceChildren();
    const title = document.createElement("p");
    title.className = "picker-title";
    title.textContent = "send to";
    pickerList.appendChild(title);
    state.hangoutUsers.forEach((u) => {
      if (u.id === state.sessionUser) return;
      const row = document.createElement("button");
      row.type = "button";
      row.className = "picker-row";
      row.appendChild(portraitEl(u.id, 28));
      const lab = document.createElement("span");
      lab.textContent = u.name;
      row.appendChild(lab);
      row.addEventListener("click", () => {
        setToast("record stub — not sent", 2000);
        setMode("carousel");
      });
      pickerList.appendChild(row);
    });
    const all = document.createElement("button");
    all.type = "button";
    all.className = "picker-row";
    all.textContent = "Everyone";
    all.addEventListener("click", () => {
      setToast("record stub — not sent", 2000);
      setMode("carousel");
    });
    pickerList.appendChild(all);
  }

  function paint() {
    if (state.mode === "carousel") paintCarousel();
    if (state.mode === "settings") paintSettings();
    if (state.mode === "picker") paintPicker();
    player.volume = state.volNotch <= 0 ? 0 : 0.35 + 0.65 * (state.volNotch / ROOMVOL_ON);
  }

  async function loadHangout() {
    const res = await fetch(state.origin + "/v1/hangout", {
      headers: authHeaders(),
    });
    if (!res.ok) throw new Error("hangout " + res.status);
    const data = await res.json();
    state.hangoutUsers = data.users || [];
  }

  async function reloadInbox() {
    const res = await fetch(state.origin + "/v1/inbox", {
      headers: sessionHeaders(),
    });
    if (!res.ok) return;
    const data = await res.json();
    const keep = currentMsg()?.seq;
    state.inbox = data.messages || [];
    state.focus = 0;
    if (keep) {
      const i = state.inbox.findIndex((m) => m.seq === keep);
      if (i >= 0) state.focus = i;
    }
    if (data.profile) {
      state.profile = data.profile;
      const u = state.hangoutUsers.find((x) => x.id === state.sessionUser);
      if (u) u.profile = data.profile;
    }
    paint();
  }

  async function saveProfile() {
    const res = await fetch(state.origin + "/v1/profile", {
      method: "PUT",
      headers: {
        ...sessionHeaders(),
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        avatar_slot: state.profile.avatar_slot,
        accent_hex: state.profile.accent_hex,
      }),
    });
    if (!res.ok) {
      setToast("profile save failed", 2000);
      return;
    }
    const data = await res.json();
    if (data.profile) {
      state.profile = data.profile;
      const u = state.hangoutUsers.find((x) => x.id === state.sessionUser);
      if (u) u.profile = data.profile;
    }
  }

  function pickAccent(hex) {
    state.profile.accent_hex = hex.toUpperCase();
    const u = state.hangoutUsers.find((x) => x.id === state.sessionUser);
    if (u) {
      u.profile = { ...state.profile };
    }
    saveProfile().then(() => paintSettings());
  }

  function pickAvatar(slot) {
    state.profile.avatar_slot = slot;
    const u = state.hangoutUsers.find((x) => x.id === state.sessionUser);
    if (u) {
      u.profile = { ...state.profile };
    }
    saveProfile().then(() => paintSettings());
  }

  function connectWs() {
    const base = state.origin.replace(/^http/, "ws");
    state.ws = new WebSocket(base + "/v1/ws");
    state.ws.onopen = () => {
      state.ws.send(JSON.stringify({ type: "hello", token: state.token }));
    };
    state.ws.onmessage = (ev) => {
      let msg;
      try {
        msg = JSON.parse(ev.data);
      } catch {
        return;
      }
      if (msg.type === "inbox") {
        setToast("new mail", 1500);
        reloadInbox().catch(() => {});
      }
    };
  }

  async function login() {
    setupStatus.textContent = "…";
    state.origin = $("origin").value.replace(/\/$/, "");
    state.token = $("token").value.trim();
    const userId = $("user").value.trim();
    const pin = $("pin").value.trim();
    const res = await fetch(state.origin + "/v1/session/login", {
      method: "POST",
      headers: {
        ...authHeaders(),
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ user_id: userId, pin }),
    });
    const data = await res.json();
    if (!data.ok) {
      setupStatus.textContent = "login failed";
      return;
    }
    state.sessionUser = userId;
    state.inbox = data.messages || [];
    state.focus = 0;
    if (data.profile) state.profile = data.profile;
    await loadHangout();
    const u = state.hangoutUsers.find((x) => x.id === userId);
    if (u && data.profile) u.profile = data.profile;
    setupPanel.classList.add("hidden");
    setupStatus.textContent = "";
    lcd.classList.remove("busy");
    setMode("carousel");
    connectWs();
    if (!state.sendHintShown) {
      state.sendHintShown = true;
      setToast("tap circle to send", 3000);
    }
    lineEl.textContent = "signed in as " + userId;
  }

  function signOut() {
    player.pause();
    state.playing = false;
    if (state.ws) state.ws.close();
    state.sessionUser = "";
    state.inbox = [];
    setupPanel.classList.remove("hidden");
    lcd.classList.add("busy");
    setMode("login");
    lineEl.textContent = "signed out";
  }

  function shoulderPress() {
    if (state.mode === "carousel") {
      player.pause();
      state.playing = false;
      setMode("settings");
    } else if (state.mode === "settings" || state.mode === "picker") {
      setMode("carousel");
    }
  }

  function circleTap() {
    if (state.mode === "carousel") {
      if (state.playing) return;
      if (Date.now() - state.carouselReadyAt < CIRCLE_DEBOUNCE_MS) return;
      setMode("picker");
    } else if (state.mode === "picker") {
      setMode("carousel");
    }
  }

  async function togglePlay() {
    const m = currentMsg();
    if (!m) return;
    if (!player.paused && state.playing) {
      player.pause();
      state.playing = false;
      m.position_ms = Math.round(player.currentTime * 1000);
      paint();
      return;
    }
    const blobRes = await fetch(
      state.origin + "/v1/messages/" + m.seq + "/blob",
      { headers: sessionHeaders() },
    );
    if (!blobRes.ok) {
      setToast("blob " + blobRes.status, 2000);
      return;
    }
    const url = URL.createObjectURL(await blobRes.blob());
    player.src = url;
    player.currentTime = (m.position_ms || 0) / 1000;
    await player.play();
    state.playing = true;
    if (!m.read) {
      await fetch(state.origin + "/v1/messages/" + m.seq + "/read", {
        method: "PUT",
        headers: {
          ...sessionHeaders(),
          "Content-Type": "application/json",
        },
        body: JSON.stringify({ position_ms: m.position_ms || 0 }),
      });
      m.read = true;
    }
    paint();
  }

  function shiftFocus(dir) {
    if (!state.inbox.length) return;
    player.pause();
    state.playing = false;
    const next = state.focus + dir;
    if (next < 0 || next >= state.inbox.length) return;
    state.focus = next;
    fetch(state.origin + "/v1/session/view", {
      method: "PUT",
      headers: {
        ...sessionHeaders(),
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ seq: state.inbox[state.focus].seq }),
    }).catch(() => {});
    paint();
  }

  $("login-btn").addEventListener("click", () => {
    login().catch((e) => {
      setupStatus.textContent = String(e);
    });
  });

  $("sign-out").addEventListener("click", signOut);
  bootEl.addEventListener("click", shoulderPress);
  circleEl.addEventListener("click", circleTap);
  peekLeft.addEventListener("click", () => shiftFocus(-1));
  peekRight.addEventListener("click", () => shiftFocus(1));
  playBtn.addEventListener("click", () => togglePlay().catch(console.error));

  volSlider.addEventListener("input", () => {
    state.volNotch = Number(volSlider.value);
    volNum.textContent = String(roomvolCodec(state.volNotch));
    paint();
  });

  scrubEl.addEventListener("input", () => {
    const m = currentMsg();
    if (!m) return;
    m.position_ms = Number(scrubEl.value);
    if (state.playing) player.currentTime = m.position_ms / 1000;
    paintCarousel();
  });

  player.addEventListener("timeupdate", () => {
    if (!state.playing) return;
    const m = currentMsg();
    if (!m) return;
    m.position_ms = Math.round(player.currentTime * 1000);
    paintCarousel();
  });

  player.addEventListener("ended", () => {
    state.playing = false;
    const m = currentMsg();
    if (m) m.position_ms = m.duration_ms || m.position_ms;
    paint();
  });

  muteEl.addEventListener("click", () => {
    const on = muteEl.getAttribute("aria-pressed") === "true";
    muteEl.setAttribute("aria-pressed", on ? "false" : "true");
  });

  document.addEventListener("keydown", (ev) => {
    if (ev.target.matches("input")) return;
    const k = ev.key.toLowerCase();
    if (k === "b" || k === "n" || ev.key === "ArrowRight") {
      ev.preventDefault();
      shoulderPress();
    }
    if (k === "c" || k === " ") {
      ev.preventDefault();
      circleTap();
    }
    if (k === "p") togglePlay().catch(() => {});
    if (ev.key === "ArrowLeft") shiftFocus(-1);
    if (ev.key === "ArrowRight") shiftFocus(1);
  });

  document.querySelectorAll(".scale-btn").forEach((btn) => {
    btn.addEventListener("click", () => {
      document.documentElement.style.setProperty(
        "--scale",
        btn.dataset.scale,
      );
      document
        .querySelectorAll(".scale-btn")
        .forEach((b) => b.classList.toggle("on", b === btn));
    });
  });

  lcd.classList.add("busy");
  lineEl.textContent = "login to start";
})();
