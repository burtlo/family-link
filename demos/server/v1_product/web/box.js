/* v1 product BOX-3 twin — carousel, shoulder settings, profile API */
(() => {
  const ROOMVOL_FIRST = 78;
  const ROOMVOL_STEP = 2;
  const ROOMVOL_MAX = 100;
  const ROOMVOL_ON = ((ROOMVOL_MAX - ROOMVOL_FIRST) / ROOMVOL_STEP) + 1;
  const CIRCLE_DEBOUNCE_MS = 400;
  const DIM_MS = 120000;
  const SLEEP_MS = 300000;

  const ACCENTS = [
    "#5AA0E8", "#E8C040", "#7AC47A", "#C070E8", "#E87A9A",
    "#7AD4E8", "#D4A0E8", "#E8A87A", "#4ECDC4", "#FF6B6B",
  ];

  const DEFAULT_ACCENTS = ACCENTS;

  /* Vertical gradients only — p13 grad 1, 2 (colors), 5. */
  const CARD_GRADS = [
    { id: 1, label: "sky fade", top: "#5AA0E8", bottom: "#101418", light: false },
    { id: 2, label: "ember", top: "#E85A5A", bottom: "#E8C040", light: false },
    { id: 5, label: "platinum", top: "#F0F2F5", bottom: "#8898A8", light: true },
  ];

  /* p13 PAGE_SCROLL_H_SNAP_GRAD */
  const SNAP_CARD_W = 132;
  const SNAP_CARD_H = 100;
  const SNAP_CARD_GAP = 12;
  const SNAP_SCROLL_MS_MIN = 380;
  const SNAP_SCROLL_MS_MAX = 720;

  const $ = (id) => document.getElementById(id);

  const lcd = $("lcd");
  const countEl = $("count");
  const ribbonToast = $("ribbon-toast");
  const screenCarousel = $("screen-carousel");
  const screenSettings = $("screen-settings");
  const screenPicker = $("screen-picker");
  const carouselTrack = $("carousel-track");
  const carouselTransport = $("carousel-transport");
  const carouselPlay = $("carousel-play");
  const carouselPlayIcon = $("carousel-play-icon");
  const carouselScrub = $("carousel-scrub");
  const pickerList = $("picker-list");
  const settingsScroll = $("settings-scroll");
  const volSlider = $("vol-slider");
  const volNum = $("vol-num");
  const swatchGrid = $("swatch-grid");
  const gradGrid = $("grad-grid");
  const faceGrid = $("face-grid");
  const settingsName = $("settings-name");
  const setupPanel = $("setup");
  const setupStatus = $("setup-status");
  const lineEl = $("line");
  const bootEl = $("boot");
  const circleEl = $("circle");
  const muteEl = $("mute");
  const sleepOverlay = $("sleep-overlay");
  const sleepBadge = $("sleep-badge");

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
    cardGrad: Number(localStorage.getItem("fl-card-grad")) || 1,
    scrollLock: false,
    carouselLocked: true,
    snapAnim: null,
    activityAt: Date.now(),
    dimmed: false,
    asleep: false,
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

  function unreadCount() {
    return state.inbox.filter((m) => !m.read).length;
  }

  function noteActivity() {
    state.activityAt = Date.now();
    if (state.asleep || state.dimmed) {
      state.asleep = false;
      state.dimmed = false;
      updateSleepVisuals();
    }
  }

  function updateSleepVisuals() {
    lcd.classList.toggle("dimmed", state.dimmed && !state.asleep);
    lcd.classList.toggle("asleep", state.asleep);
    if (state.asleep) {
      sleepOverlay.classList.remove("hidden");
      sleepOverlay.setAttribute("aria-hidden", "false");
      const accent = state.sessionUser
        ? state.profile.accent_hex
        : ACCENTS[0];
      sleepOverlay.style.setProperty("--sleep-accent", accent);
      const n = unreadCount();
      if (n > 0 && state.sessionUser) {
        sleepBadge.classList.remove("hidden");
        sleepBadge.textContent = n > 9 ? "9+" : String(n);
      } else {
        sleepBadge.classList.add("hidden");
      }
    } else {
      sleepOverlay.classList.add("hidden");
      sleepOverlay.setAttribute("aria-hidden", "true");
      sleepBadge.classList.add("hidden");
    }
  }

  function tickSleepPolicy() {
    if (state.mode === "login") {
      return;
    }
    const idle = Date.now() - state.activityAt;
    if (!state.asleep && idle > SLEEP_MS) {
      state.asleep = true;
      state.dimmed = false;
      player.pause();
      state.playing = false;
      updateSleepVisuals();
    } else if (!state.asleep && !state.dimmed && idle > DIM_MS) {
      state.dimmed = true;
      updateSleepVisuals();
    }
  }

  function setToast(msg, ms = 2500) {
    ribbonToast.textContent = msg || "";
    if (msg && ms > 0) {
      setTimeout(() => {
        if (ribbonToast.textContent === msg) {
          ribbonToast.textContent = "";
        }
      }, ms);
    }
  }

  function setMode(mode) {
    noteActivity();
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
    }
    paint();
  }

  function currentMsg() {
    return state.inbox[state.focus] || null;
  }

  function displayName(label) {
    const u = state.hangoutUsers.find(
      (x) => x.name === label || x.id === label,
    );
    return u ? u.name : label || "Family";
  }

  function cardGradStyle() {
    const g = CARD_GRADS.find((x) => x.id === state.cardGrad) || CARD_GRADS[0];
    return {
      ...g,
      css: `linear-gradient(180deg, ${g.top} 0%, ${g.bottom} 100%)`,
    };
  }

  function buildMsgCard(msg, idx) {
    const card = document.createElement("button");
    card.type = "button";
    card.className = "msg-card";
    card.dataset.idx = String(idx);
    const grad = cardGradStyle();
    card.style.background = grad.css;
    if (grad.light) card.classList.add("grad-light");

    if (!msg) {
      const empty = document.createElement("div");
      empty.className = "card-name";
      empty.textContent = "empty";
      card.appendChild(empty);
      return card;
    }

    const uid = userIdForLabel(senderKey(msg));
    const head = document.createElement("div");
    head.className = "card-head";
    head.appendChild(portraitEl(uid, 22));
    const nameEl = document.createElement("div");
    nameEl.className = "card-name";
    nameEl.textContent = displayName(senderKey(msg));
    head.appendChild(nameEl);
    card.appendChild(head);
    return card;
  }

  function updateCountRibbon() {
    const n = state.inbox.length;
    countEl.textContent = n > 0 ? `${state.focus + 1}/${n}` : "0/0";
  }

  function cardAt(idx) {
    return carouselTrack.querySelector(`.msg-card[data-idx="${idx}"]`);
  }

  function focusIndexFromScroll() {
    if (!state.inbox.length) return 0;
    const mid = carouselTrack.scrollLeft + carouselTrack.clientWidth / 2;
    let best = 0;
    let bestDist = Infinity;
    for (let i = 0; i < state.inbox.length; i++) {
      const el = cardAt(i);
      if (!el) continue;
      const center = el.offsetLeft + el.offsetWidth / 2;
      const dist = Math.abs(center - mid);
      if (dist < bestDist) {
        bestDist = dist;
        best = i;
      }
    }
    return best;
  }

  function updateCardCenters() {
    const moving = state.scrollLock || !!state.snapAnim;
    const highlight = moving ? focusIndexFromScroll() : state.focus;
    carouselTrack.querySelectorAll(".msg-card").forEach((card) => {
      const idx = Number(card.dataset.idx);
      const center = idx === highlight;
      card.classList.toggle("center", center);
      card.disabled = !moving && center;
    });
    carouselTransport.classList.toggle(
      "locked-off",
      !state.carouselLocked || state.scrollLock,
    );
  }

  function updatePlayIcon() {
    carouselPlayIcon.className =
      "play-icon " + (state.playing && !player.paused ? "pause" : "play");
  }

  function updateTransport() {
    const m = currentMsg();
    updateCardCenters();
    updatePlayIcon();
    if (!m) {
      carouselScrub.max = "1";
      carouselScrub.value = "0";
      return;
    }
    const dur = m.duration_ms > 0 ? m.duration_ms : 1;
    carouselScrub.max = String(dur);
    carouselScrub.value = String(
      state.playing ? Math.round(player.currentTime * 1000) : m.position_ms || 0,
    );
  }

  function maxScrollLeft() {
    return Math.max(0, carouselTrack.scrollWidth - carouselTrack.clientWidth);
  }

  function clampScrollLeft(x) {
    const max = maxScrollLeft();
    if (x < 0) return 0;
    if (x > max) return max;
    return x;
  }

  /* p13 snap_target_x — horizontal clamp */
  function snapTargetScrollLeft(card) {
    if (!card) return 0;
    const cardCenter = card.offsetLeft + card.offsetWidth / 2;
    return clampScrollLeft(cardCenter - carouselTrack.clientWidth / 2);
  }

  function easeInOut(t) {
    return t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
  }

  function snapDurationMs(from, to) {
    const dist = Math.abs(to - from);
    return Math.min(
      SNAP_SCROLL_MS_MAX,
      Math.max(SNAP_SCROLL_MS_MIN, 300 + dist * 0.55),
    );
  }

  function snapScrollTo(target, animate) {
    const clamped = clampScrollLeft(target);
    if (state.snapAnim) {
      cancelAnimationFrame(state.snapAnim);
      state.snapAnim = null;
    }
    carouselTrack.classList.remove("is-snapping");
    if (!animate) {
      carouselTrack.scrollLeft = clamped;
      state.scrollLock = false;
      state.carouselLocked = true;
      updateTransport();
      return;
    }
    const start = carouselTrack.scrollLeft;
    if (Math.abs(start - clamped) < 1) {
      carouselTrack.scrollLeft = clamped;
      state.scrollLock = false;
      state.carouselLocked = true;
      updateTransport();
      return;
    }
    state.scrollLock = true;
    state.carouselLocked = false;
    carouselTrack.classList.add("is-snapping");
    updateTransport();
    const duration = snapDurationMs(start, clamped);
    const t0 = performance.now();
    function frame(now) {
      const p = Math.min(1, (now - t0) / duration);
      carouselTrack.scrollLeft = start + (clamped - start) * easeInOut(p);
      updateCardCenters();
      if (p < 1) {
        state.snapAnim = requestAnimationFrame(frame);
      } else {
        carouselTrack.scrollLeft = clamped;
        carouselTrack.classList.remove("is-snapping");
        state.snapAnim = null;
        state.scrollLock = false;
        state.carouselLocked = true;
        updateTransport();
      }
    }
    state.snapAnim = requestAnimationFrame(frame);
  }

  function scrollToFocus(animate) {
    snapScrollTo(snapTargetScrollLeft(cardAt(state.focus)), animate);
  }

  function persistView() {
    const m = currentMsg();
    if (!m) return;
    fetch(state.origin + "/v1/session/view", {
      method: "PUT",
      headers: {
        ...sessionHeaders(),
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ seq: m.seq }),
    }).catch(() => {});
  }

  function setFocus(idx, smoothScroll) {
    if (!state.inbox.length || idx < 0 || idx >= state.inbox.length) return;
    if (idx === state.focus && smoothScroll !== true) return;
    player.pause();
    state.playing = false;
    state.focus = idx;
    updateCountRibbon();
    updateTransport();
    persistView();
    if (smoothScroll === true) {
      scrollToFocus(true);
    } else if (smoothScroll === false) {
      scrollToFocus(false);
    }
  }

  function syncFocusFromScroll() {
    if (state.scrollLock || !state.inbox.length) return;
    const mid = carouselTrack.scrollLeft + carouselTrack.clientWidth / 2;
    let best = state.focus;
    let bestDist = Infinity;
    for (let i = 0; i < state.inbox.length; i++) {
      const el = cardAt(i);
      if (!el) continue;
      const center = el.offsetLeft + el.offsetWidth / 2;
      const dist = Math.abs(center - mid);
      if (dist < bestDist) {
        bestDist = dist;
        best = i;
      }
    }
    if (best === state.focus) return;
    setFocus(best);
  }

  function paintCarousel(opts = {}) {
    const scroll = opts.scroll !== false;
    const animate = opts.smooth === true;

    carouselTrack.replaceChildren();
    if (!state.inbox.length) {
      carouselTrack.appendChild(buildMsgCard(null, 0));
      const end = document.createElement("div");
      end.className = "carousel-end";
      carouselTrack.appendChild(end);
      updateCountRibbon();
      updateTransport();
      return;
    }

    state.inbox.forEach((msg, idx) => {
      carouselTrack.appendChild(buildMsgCard(msg, idx));
    });
    const end = document.createElement("div");
    end.className = "carousel-end";
    carouselTrack.appendChild(end);

    updateCountRibbon();
    updateTransport();
    if (scroll) scrollToFocus(animate);
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

    gradGrid.replaceChildren();
    const activeGrad = state.cardGrad;
    CARD_GRADS.forEach((g) => {
      const b = document.createElement("button");
      b.type = "button";
      b.className =
        "grad-btn" +
        (g.id === activeGrad ? " on" : "") +
        (g.light ? " light" : "");
      b.style.background = `linear-gradient(180deg, ${g.top} 0%, ${g.bottom} 100%)`;
      const lab = document.createElement("span");
      lab.textContent = g.label;
      b.appendChild(lab);
      b.addEventListener("click", () => pickCardGrad(g.id));
      gradGrid.appendChild(b);
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

  function pickCardGrad(id) {
    state.cardGrad = id;
    localStorage.setItem("fl-card-grad", String(id));
    paintSettings();
    if (state.mode === "carousel") paintCarousel({ scroll: false });
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
    state.activityAt = Date.now();
    state.dimmed = false;
    state.asleep = false;
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
    state.asleep = false;
    state.dimmed = false;
    updateSleepVisuals();
    if (state.ws) state.ws.close();
    state.sessionUser = "";
    state.inbox = [];
    setupPanel.classList.remove("hidden");
    lcd.classList.add("busy");
    setMode("login");
    lineEl.textContent = "signed out";
  }

  function shoulderPress() {
    if (state.asleep) {
      noteActivity();
      return;
    }
    noteActivity();
    if (state.mode === "carousel") {
      player.pause();
      state.playing = false;
      setMode("settings");
    } else if (state.mode === "settings" || state.mode === "picker") {
      setMode("carousel");
    }
  }

  function circleTap() {
    if (state.asleep) {
      noteActivity();
      return;
    }
    noteActivity();
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
      updateTransport();
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
    updateTransport();
  }

  function shiftFocus(dir) {
    setFocus(state.focus + dir, true);
  }

  lcd.addEventListener("pointerdown", () => noteActivity());

  setInterval(tickSleepPolicy, 500);

  $("login-btn").addEventListener("click", () => {
    login().catch((e) => {
      setupStatus.textContent = String(e);
    });
  });

  $("sign-out").addEventListener("click", signOut);
  bootEl.addEventListener("click", shoulderPress);
  circleEl.addEventListener("click", circleTap);
  carouselTrack.addEventListener("click", (ev) => {
    const card = ev.target.closest(".msg-card");
    if (!card || card.disabled) return;
    const idx = Number(card.dataset.idx);
    if (Number.isNaN(idx) || idx === state.focus) return;
    setFocus(idx, true);
  });

  let scrollEndTimer;
  carouselTrack.addEventListener("scroll", () => {
    if (!state.scrollLock) {
      carouselTrack.scrollLeft = clampScrollLeft(carouselTrack.scrollLeft);
      updateCardCenters();
    }
    window.clearTimeout(scrollEndTimer);
    scrollEndTimer = window.setTimeout(() => {
      if (state.scrollLock) return;
      syncFocusFromScroll();
      scrollToFocus(true);
    }, 80);
  });

  carouselPlay.addEventListener("click", () => {
    if (carouselTransport.classList.contains("locked-off")) return;
    togglePlay().catch(console.error);
  });

  carouselScrub.addEventListener("input", () => {
    if (carouselTransport.classList.contains("locked-off")) return;
    const m = currentMsg();
    if (!m) return;
    m.position_ms = Number(carouselScrub.value);
    if (state.playing) player.currentTime = m.position_ms / 1000;
  });

  volSlider.addEventListener("input", () => {
    state.volNotch = Number(volSlider.value);
    volNum.textContent = String(roomvolCodec(state.volNotch));
    paint();
  });

  player.addEventListener("timeupdate", () => {
    if (!state.playing) return;
    const m = currentMsg();
    if (!m) return;
    m.position_ms = Math.round(player.currentTime * 1000);
    carouselScrub.value = String(m.position_ms);
  });

  player.addEventListener("ended", () => {
    state.playing = false;
    const m = currentMsg();
    if (m) m.position_ms = m.duration_ms || m.position_ms;
    updateTransport();
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
