/* v1 product BOX-3 twin — carousel, shoulder settings, profile API */
import {
  V1_CAROUSEL_SNAP_MS_MAX,
  V1_CAROUSEL_SNAP_MS_MIN,
  V1_UI_DIM_MS,
  V1_UI_IDLE_RELOCK_MS,
  V1_UI_SLEEP_MS,
  V1_UI_TOAST_MS,
  V1_SEND_RECEIPT_MS,
} from "./v1_timing.js";

(() => {
  const ROOMVOL_FIRST = 78;
  const ROOMVOL_STEP = 2;
  const ROOMVOL_MAX = 100;
  const ROOMVOL_ON = ((ROOMVOL_MAX - ROOMVOL_FIRST) / ROOMVOL_STEP) + 1;
  const CIRCLE_DEBOUNCE_MS = 400;
  const DIM_MS = V1_UI_DIM_MS;
  const SLEEP_MS = V1_UI_SLEEP_MS;
  const IDLE_RELOCK_MS = V1_UI_IDLE_RELOCK_MS;

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

  const $ = (id) => document.getElementById(id);

  const lcd = $("lcd");
  const countEl = $("count");
  const ribbonToast = $("ribbon-toast");
  const screenRoster = $("screen-roster");
  const rosterTrack = $("roster-track");
  const screenCarousel = $("screen-carousel");
  const screenModal = $("screen-modal");
  const screenPicker = $("screen-picker");
  const screenSend = $("screen-send");
  const sendHeadline = $("send-headline");
  const sendCheck = $("send-check");
  const sendFaces = $("send-faces");
  const sendNames = $("send-names");
  const carouselTrack = $("carousel-track");
  const carouselTransport = $("carousel-transport");
  const carouselFromPrefix = $("carousel-from-prefix");
  const carouselFromFace = $("carousel-from-face");
  const carouselFromName = $("carousel-from-name");
  const carouselHeader = $("carousel-header");
  const carouselPlay = $("carousel-play");
  const carouselPlayIcon = $("carousel-play-icon");
  const carouselScrub = $("carousel-scrub");
  const pickerTrack = $("picker-track");
  const pickerRec = $("picker-rec");
  const pickerRecDisk = $("picker-rec-disk");
  const modalDim = $("modal-dim");
  const modalTitle = $("modal-title");
  const modalClose = $("modal-close");
  const modalBody = $("modal-body");
  const setupPanel = $("setup");
  const setupStatus = $("setup-status");
  const lineEl = $("line");
  const bootEl = $("boot");
  const circleEl = $("circle");
  const muteEl = $("mute");
  const sleepOverlay = $("sleep-overlay");
  const sleepBadge = $("sleep-badge");

  const SETTINGS_CARDS = [
    { id: "logout", title: "Logout" },
    { id: "volume", title: "Volume" },
    { id: "autoplay", title: "Auto-play new" },
    { id: "icons", title: "User icons" },
    { id: "colors", title: "User colors" },
    { id: "card", title: "Card colors" },
  ];
  const PICK_CIRCLE_D = 54;

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
    settingsFocus: 0,
    modalKind: null,
    rosterFocus: 0,
    volNotch: ROOMVOL_ON,
    ws: null,
    playing: false,
    scrubbing: false,
    carouselReadyAt: 0,
    sendHintShown: false,
    profile: { avatar_slot: 0, accent_hex: ACCENTS[0], autoplay_new: false },
    cardGrad: Number(localStorage.getItem("fl-card-grad")) || 1,
    scrollLock: false,
    carouselLocked: true,
    snapAnim: null,
    rosterSnapAnim: null,
    rosterScrollLock: false,
    pickerFocus: 0,
    pickerSelected: new Set(),
    pickerSnapAnim: null,
    pickerScrollLock: false,
    sendRecipients: [],
    sendBroadcast: false,
    activityAt: Date.now(),
    dimmed: false,
    asleep: false,
    privacyScreenOff: false,
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
      autoplay_new: false,
    };
    }
    return {
      avatar_slot: u.profile.avatar_slot || 0,
      accent_hex: u.profile.accent_hex || defaultAccentForUser(userId),
      autoplay_new: !!u.profile.autoplay_new,
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
    if (state.privacyScreenOff) {
      return;
    }
    state.activityAt = Date.now();
    if (state.asleep || state.dimmed) {
      state.asleep = false;
      state.dimmed = false;
      updateSleepVisuals();
    }
  }

  function updateSleepVisuals() {
    lcd.classList.toggle("privacy-off", state.privacyScreenOff);
    lcd.classList.toggle("dimmed", state.dimmed && !state.asleep && !state.privacyScreenOff);
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
    if (state.mode === "login" || state.privacyScreenOff) {
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

  function setToast(msg, ms = V1_UI_TOAST_MS) {
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
    if (mode !== "settings") {
      closeModal();
    }
    screenRoster.classList.toggle("hidden", mode !== "login");
    screenCarousel.classList.toggle(
      "hidden",
      mode !== "carousel" && mode !== "settings",
    );
    screenModal.classList.toggle("hidden", !state.modalKind);
    screenPicker.classList.toggle("hidden", mode !== "picker");
    screenSend.classList.toggle("hidden", mode !== "send");

    if (mode === "login") {
      lcd.classList.add("busy");
      countEl.textContent = "";
      paintRoster();
      return;
    }
    lcd.classList.remove("busy");

    if (mode === "carousel") {
      state.carouselReadyAt = Date.now();
    }
    if (mode === "settings") {
      state.settingsFocus = 0;
    }
    if (mode === "picker") {
      state.pickerFocus = 0;
      state.pickerSelected = new Set();
    }
    paint();
  }

  function currentMsg() {
    return state.inbox[state.focus] || null;
  }

  function formatDurationMs(ms) {
    const total = Math.max(0, Math.floor((ms || 0) / 1000));
    const min = Math.floor(total / 60);
    const sec = total % 60;
    return `${min}:${String(sec).padStart(2, "0")}`;
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
    const face = document.createElement("div");
    face.className = "msg-card-face";
    face.appendChild(portraitEl(uid, 66));
    card.appendChild(face);

    const unread = document.createElement("span");
    unread.className = "msg-card-unread";
    unread.hidden = !!msg.read;
    unread.setAttribute("aria-hidden", msg.read ? "true" : "false");
    card.appendChild(unread);

    const dur = document.createElement("span");
    dur.className = "msg-card-duration";
    dur.textContent = formatDurationMs(msg.duration_ms);
    card.appendChild(dur);

    return card;
  }

  function buildSettingCard(item, idx) {
    const card = document.createElement("button");
    card.type = "button";
    card.className = "msg-card setting-card";
    card.dataset.idx = String(idx);
    card.dataset.setting = item.id;
    const grad = cardGradStyle();
    card.style.background = grad.css;
    if (grad.light) card.classList.add("grad-light");
    const lab = document.createElement("div");
    lab.className = "setting-card-label";
    lab.textContent = item.title;
    card.appendChild(lab);
    return card;
  }

  function buildUserCard(user, idx) {
    const card = document.createElement("button");
    card.type = "button";
    card.className = "msg-card user-card";
    card.dataset.idx = String(idx);
    card.dataset.userId = user.id;
    const grad = cardGradStyle();
    card.style.background = grad.css;
    if (grad.light) card.classList.add("grad-light");
    card.appendChild(portraitEl(user.id, 66));
    const name = document.createElement("div");
    name.className = "user-card-name";
    name.textContent = user.name || user.id;
    card.appendChild(name);
    return card;
  }

  function rosterCardAt(idx) {
    return rosterTrack.querySelector(`.user-card[data-idx="${idx}"]`);
  }

  function rosterFocusFromScroll() {
    const users = rosterUsersOrdered();
    if (!users.length) return 0;
    const mid = rosterTrack.scrollLeft + rosterTrack.clientWidth / 2;
    let best = 0;
    let bestDist = Infinity;
    for (let i = 0; i < users.length; i++) {
      const el = rosterCardAt(i);
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

  function rosterScrollAlignedToFocus() {
    const card = rosterCardAt(state.rosterFocus);
    if (!card) return false;
    const target = rosterSnapTarget(card);
    return Math.abs(rosterTrack.scrollLeft - target) < 2;
  }

  function updateRosterCenters() {
    const snapping = state.rosterScrollLock || !!state.rosterSnapAnim;
    const highlight =
      snapping || rosterScrollAlignedToFocus()
        ? state.rosterFocus
        : rosterFocusFromScroll();
    rosterTrack.querySelectorAll(".user-card").forEach((card) => {
      const idx = Number(card.dataset.idx);
      card.classList.toggle("center", idx === highlight);
    });
  }

  function rosterSnapTarget(card) {
    if (!card) return 0;
    const cardCenter = card.offsetLeft + card.offsetWidth / 2;
    const max = Math.max(0, rosterTrack.scrollWidth - rosterTrack.clientWidth);
    return Math.max(0, Math.min(max, cardCenter - rosterTrack.clientWidth / 2));
  }

  function rosterSnapTo(target, animate) {
    const max = Math.max(0, rosterTrack.scrollWidth - rosterTrack.clientWidth);
    const clamped = Math.max(0, Math.min(max, target));
    if (state.rosterSnapAnim) {
      cancelAnimationFrame(state.rosterSnapAnim);
      state.rosterSnapAnim = null;
    }
    rosterTrack.classList.remove("is-snapping");
    state.rosterScrollLock = false;
    if (!animate) {
      rosterTrack.scrollLeft = clamped;
      updateRosterCenters();
      return;
    }
    const start = rosterTrack.scrollLeft;
    if (Math.abs(start - clamped) < 1) {
      rosterTrack.scrollLeft = clamped;
      updateRosterCenters();
      return;
    }
    state.rosterScrollLock = true;
    rosterTrack.classList.add("is-snapping");
    updateRosterCenters();
    const duration = snapDurationMs(start, clamped);
    const t0 = performance.now();
    function frame(now) {
      const p = Math.min(1, (now - t0) / duration);
      rosterTrack.scrollLeft = start + (clamped - start) * easeInOut(p);
      updateRosterCenters();
      if (p < 1) {
        state.rosterSnapAnim = requestAnimationFrame(frame);
      } else {
        state.rosterSnapAnim = null;
        state.rosterScrollLock = false;
        rosterTrack.classList.remove("is-snapping");
        rosterTrack.scrollLeft = clamped;
        updateRosterCenters();
      }
    }
    state.rosterSnapAnim = requestAnimationFrame(frame);
  }

  function loginOrderIds() {
    try {
      const raw = localStorage.getItem("fl-login-order");
      const parsed = raw ? JSON.parse(raw) : [];
      return Array.isArray(parsed) ? parsed.filter((x) => typeof x === "string") : [];
    } catch {
      return [];
    }
  }

  function noteLoginOrder(userId) {
    if (!userId) return;
    const next = [userId, ...loginOrderIds().filter((id) => id !== userId)];
    localStorage.setItem("fl-login-order", JSON.stringify(next.slice(0, 8)));
  }

  function sendOrderIds() {
    try {
      const raw = localStorage.getItem("fl-send-order");
      const parsed = raw ? JSON.parse(raw) : [];
      return Array.isArray(parsed) ? parsed.filter((x) => typeof x === "string") : [];
    } catch {
      return [];
    }
  }

  function noteSendOrder(userId) {
    if (!userId) return;
    const next = [userId, ...sendOrderIds().filter((id) => id !== userId)];
    localStorage.setItem("fl-send-order", JSON.stringify(next.slice(0, 8)));
  }

  /** Recent outbound recipients first; excludes signed-in user. */
  function pickerUsersOrdered() {
    const users = state.hangoutUsers.filter((u) => u.id !== state.sessionUser);
    const order = sendOrderIds();
    if (!order.length) return users;
    const byId = new Map(users.map((u) => [u.id, u]));
    const out = [];
    const used = new Set();
    for (const id of order) {
      const u = byId.get(id);
      if (u && !used.has(id)) {
        out.push(u);
        used.add(id);
      }
    }
    for (const u of users) {
      if (!used.has(u.id)) out.push(u);
    }
    return out;
  }

  /** Most recent login first; hangout order for everyone else / cold start. */
  function rosterUsersOrdered() {
    const users = state.hangoutUsers.slice();
    const order = loginOrderIds();
    if (!order.length) return users;
    const byId = new Map(users.map((u) => [u.id, u]));
    const out = [];
    const used = new Set();
    for (const id of order) {
      const u = byId.get(id);
      if (u && !used.has(id)) {
        out.push(u);
        used.add(id);
      }
    }
    for (const u of users) {
      if (!used.has(u.id)) out.push(u);
    }
    return out;
  }

  function paintRoster() {
    rosterTrack.replaceChildren();
    const users = rosterUsersOrdered();
    const pref = ($("user").value || "").trim();
    state.rosterFocus = 0;
    users.forEach((u, i) => {
      if (u.id === pref || u.name === pref) state.rosterFocus = i;
      const card = buildUserCard(u, i);
      card.addEventListener("click", () => {
        if (i !== state.rosterFocus) {
          state.rosterFocus = i;
          rosterSnapTo(rosterSnapTarget(card), true);
          return;
        }
        $("user").value = u.id;
        lineEl.textContent = "picked " + (u.name || u.id) + " — enter PIN to login";
      });
      rosterTrack.appendChild(card);
    });
    const end = document.createElement("div");
    end.className = "carousel-end";
    rosterTrack.appendChild(end);
    requestAnimationFrame(() => {
      const focusCard = rosterCardAt(state.rosterFocus);
      rosterSnapTo(rosterSnapTarget(focusCard), false);
    });
  }

  function updateCountRibbon() {
    if (state.mode === "settings") {
      countEl.textContent = `${state.settingsFocus + 1}/${SETTINGS_CARDS.length}`;
      return;
    }
    const n = state.inbox.length;
    countEl.textContent = n > 0 ? `${state.focus + 1}/${n}` : "0/0";
  }

  function activeFocus() {
    return state.mode === "settings" ? state.settingsFocus : state.focus;
  }

  function activeCount() {
    return state.mode === "settings" ? SETTINGS_CARDS.length : state.inbox.length;
  }

  function cardAt(idx) {
    return carouselTrack.querySelector(`.msg-card[data-idx="${idx}"]`);
  }

  function focusIndexFromScroll() {
    const n = activeCount();
    if (!n) return 0;
    const mid = carouselTrack.scrollLeft + carouselTrack.clientWidth / 2;
    let best = 0;
    let bestDist = Infinity;
    for (let i = 0; i < n; i++) {
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
    const highlight = moving ? focusIndexFromScroll() : activeFocus();
    const settings = state.mode === "settings";
    carouselTrack.querySelectorAll(".msg-card").forEach((card) => {
      const idx = Number(card.dataset.idx);
      const center = idx === highlight;
      card.classList.toggle("center", center);
      card.disabled = settings ? false : !moving && center;
      if (!settings && !card.classList.contains("setting-card")) {
        const msg = state.inbox[idx];
        const unread = card.querySelector(".msg-card-unread");
        if (msg && unread) {
          unread.hidden = !!msg.read;
          unread.setAttribute("aria-hidden", msg.read ? "true" : "false");
        }
      }
    });
    const lockedOff = settings || !state.carouselLocked || state.scrollLock;
    carouselTransport.classList.toggle("locked-off", lockedOff);
    carouselHeader.classList.toggle("locked-off", lockedOff);
    carouselPlay.classList.toggle("hidden", settings);
    if (carouselFromPrefix && carouselFromFace && carouselFromName) {
      if (settings) {
        carouselFromPrefix.textContent = "Settings";
        carouselFromFace.replaceChildren();
        carouselFromName.textContent = "";
        carouselFromFace.classList.add("hidden");
        carouselFromName.classList.add("hidden");
      } else {
        carouselFromFace.classList.remove("hidden");
        carouselFromName.classList.remove("hidden");
        const msg = state.inbox[highlight] || currentMsg();
        carouselFromPrefix.textContent = "From";
        if (msg) {
          const uid = userIdForLabel(senderKey(msg));
          carouselFromFace.replaceChildren();
          carouselFromFace.appendChild(portraitEl(uid, 28));
          carouselFromName.textContent = displayName(senderKey(msg));
        } else {
          carouselFromFace.replaceChildren();
          carouselFromName.textContent = "";
        }
      }
    }
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
      if (carouselFromPrefix) carouselFromPrefix.textContent = "";
      if (carouselFromFace) carouselFromFace.replaceChildren();
      if (carouselFromName) carouselFromName.textContent = "";
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
      V1_CAROUSEL_SNAP_MS_MAX,
      Math.max(V1_CAROUSEL_SNAP_MS_MIN, 300 + dist * 0.55),
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
    snapScrollTo(snapTargetScrollLeft(cardAt(activeFocus())), animate);
  }

  function persistView() {
    if (state.mode === "settings") return;
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
    const settings = state.mode === "settings";
    const n = activeCount();
    if (!n || idx < 0 || idx >= n) return;
    const cur = activeFocus();
    if (idx === cur && smoothScroll !== true) return;
    if (!settings) {
      player.pause();
      state.playing = false;
      state.focus = idx;
    } else {
      state.settingsFocus = idx;
    }
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
    if (state.scrollLock || !activeCount()) return;
    const mid = carouselTrack.scrollLeft + carouselTrack.clientWidth / 2;
    let best = activeFocus();
    let bestDist = Infinity;
    for (let i = 0; i < activeCount(); i++) {
      const el = cardAt(i);
      if (!el) continue;
      const center = el.offsetLeft + el.offsetWidth / 2;
      const dist = Math.abs(center - mid);
      if (dist < bestDist) {
        bestDist = dist;
        best = i;
      }
    }
    if (best === activeFocus()) return;
    setFocus(best);
  }

  function paintCarousel(opts = {}) {
    const scroll = opts.scroll !== false;
    const animate = opts.smooth === true;
    const keepScroll = scroll ? null : carouselTrack.scrollLeft;
    const settings = state.mode === "settings";

    carouselTrack.replaceChildren();
    if (settings) {
      SETTINGS_CARDS.forEach((item, idx) => {
        carouselTrack.appendChild(buildSettingCard(item, idx));
      });
    } else if (!state.inbox.length) {
      carouselTrack.appendChild(buildMsgCard(null, 0));
    } else {
      state.inbox.forEach((msg, idx) => {
        carouselTrack.appendChild(buildMsgCard(msg, idx));
      });
    }
    const end = document.createElement("div");
    end.className = "carousel-end";
    carouselTrack.appendChild(end);

    updateCountRibbon();
    updateTransport();
    if (scroll) {
      scrollToFocus(animate);
    } else if (keepScroll != null) {
      carouselTrack.scrollLeft = keepScroll;
    }
  }

  function facePreviewEl(slot, sizePx) {
    const wrap = document.createElement("div");
    wrap.className = "portrait";
    wrap.style.width = sizePx + "px";
    wrap.style.height = sizePx + "px";
    const accent =
      slot <= 0
        ? state.profile.accent_hex
        : ACCENTS[(slot - 1) % ACCENTS.length];
    const paintGeometry = () => {
      wrap.classList.add("geometry");
      wrap.style.background = accent;
      wrap.innerHTML = '<span class="eye l"></span><span class="eye r"></span>';
    };
    if (slot >= 1 && slot <= 12) {
      const img = document.createElement("img");
      img.src = "avatars/avatar-" + slot + ".png";
      img.alt = "";
      img.onerror = () => {
        img.remove();
        paintGeometry();
      };
      wrap.appendChild(img);
    } else {
      paintGeometry();
    }
    return wrap;
  }

  function closeModal() {
    state.modalKind = null;
    if (screenModal) screenModal.classList.add("hidden");
    if (modalBody) modalBody.replaceChildren();
  }

  function openModal(kind) {
    state.modalKind = kind;
    const item = SETTINGS_CARDS.find((c) => c.id === kind);
    if (modalTitle) modalTitle.textContent = item ? item.title : "";
    paintModalBody(kind);
    if (screenModal) screenModal.classList.remove("hidden");
  }

  function paintModalBody(kind) {
    if (!modalBody) return;
    modalBody.replaceChildren();
    if (kind === "logout") {
      const hint = document.createElement("p");
      hint.className = "settings-section";
      hint.textContent = "Sign out of this box?";
      modalBody.appendChild(hint);
      const out = document.createElement("button");
      out.type = "button";
      out.className = "sign-out";
      out.textContent = "sign out";
      out.addEventListener("click", signOut);
      modalBody.appendChild(out);
      return;
    }
    if (kind === "volume") {
      const row = document.createElement("div");
      row.className = "vol-row";
      const slider = document.createElement("input");
      slider.type = "range";
      slider.min = "0";
      slider.max = String(ROOMVOL_ON);
      slider.value = String(state.volNotch);
      const num = document.createElement("span");
      num.className = "vol-num";
      num.textContent = String(roomvolCodec(state.volNotch));
      slider.addEventListener("input", () => {
        state.volNotch = Number(slider.value);
        num.textContent = String(roomvolCodec(state.volNotch));
        player.volume =
          state.volNotch <= 0 ? 0 : 0.35 + 0.65 * (state.volNotch / ROOMVOL_ON);
      });
      row.appendChild(slider);
      row.appendChild(num);
      modalBody.appendChild(row);
      return;
    }
    if (kind === "autoplay") {
      const hint = document.createElement("p");
      hint.className = "settings-section";
      hint.textContent =
        "When you are on the newest message and caught up, play new mail automatically.";
      modalBody.appendChild(hint);
      const row = document.createElement("label");
      row.className = "autoplay-row";
      const sw = document.createElement("input");
      sw.type = "checkbox";
      sw.checked = !!state.profile.autoplay_new;
      sw.addEventListener("change", () => {
        state.profile.autoplay_new = sw.checked;
        saveProfile().catch(() => {});
      });
      row.appendChild(sw);
      row.appendChild(document.createTextNode(" Auto-play new messages"));
      modalBody.appendChild(row);
      return;
    }
    if (kind === "icons") {
      const grid = document.createElement("div");
      grid.className = "face-grid";
      for (let slot = 0; slot <= 12; slot++) {
        const b = document.createElement("button");
        b.type = "button";
        b.className =
          "face-btn" + (slot === state.profile.avatar_slot ? " on" : "");
        b.appendChild(facePreviewEl(slot, PICK_CIRCLE_D - 6));
        b.addEventListener("click", () => {
          pickAvatar(slot);
          grid.querySelectorAll(".face-btn").forEach((el, i) => {
            el.classList.toggle("on", i === slot);
          });
        });
        grid.appendChild(b);
      }
      modalBody.appendChild(grid);
      return;
    }
    if (kind === "colors") {
      const grid = document.createElement("div");
      grid.className = "swatch-grid";
      const myAccent = state.profile.accent_hex;
      ACCENTS.forEach((hex) => {
        const b = document.createElement("button");
        b.type = "button";
        b.className =
          "swatch" +
          (hex.toUpperCase() === myAccent.toUpperCase() ? " on" : "");
        b.style.background = hex;
        b.addEventListener("click", () => {
          pickAccent(hex);
          grid.querySelectorAll(".swatch").forEach((el) => {
            el.classList.toggle(
              "on",
              el.style.background.toUpperCase() === hex.toUpperCase() ||
                getComputedStyle(el).backgroundColor === hex,
            );
          });
          grid.querySelectorAll(".swatch").forEach((el) => {
            el.classList.toggle("on", el === b);
          });
        });
        grid.appendChild(b);
      });
      modalBody.appendChild(grid);
      return;
    }
    if (kind === "card") {
      const grid = document.createElement("div");
      grid.className = "grad-grid";
      CARD_GRADS.forEach((g) => {
        const b = document.createElement("button");
        b.type = "button";
        b.className =
          "grad-btn" +
          (g.id === state.cardGrad ? " on" : "") +
          (g.light ? " light" : "");
        b.style.background = `linear-gradient(180deg, ${g.top} 0%, ${g.bottom} 100%)`;
        const lab = document.createElement("span");
        lab.textContent = g.label;
        b.appendChild(lab);
        b.addEventListener("click", () => {
          pickCardGrad(g.id);
          grid.querySelectorAll(".grad-btn").forEach((el) => el.classList.remove("on"));
          b.classList.add("on");
          paintCarousel({ scroll: false });
        });
        grid.appendChild(b);
      });
      modalBody.appendChild(grid);
    }
  }

  function pickerCardAt(idx) {
    return pickerTrack.querySelector(`.picker-card[data-idx="${idx}"]`);
  }

  function pickerSnapTarget(card) {
    if (!card) return 0;
    const cardCenter = card.offsetLeft + card.offsetWidth / 2;
    const max = Math.max(0, pickerTrack.scrollWidth - pickerTrack.clientWidth);
    return Math.max(0, Math.min(max, cardCenter - pickerTrack.clientWidth / 2));
  }

  function pickerSnapTo(target, animate) {
    const max = Math.max(0, pickerTrack.scrollWidth - pickerTrack.clientWidth);
    const clamped = Math.max(0, Math.min(max, target));
    if (state.pickerSnapAnim) {
      cancelAnimationFrame(state.pickerSnapAnim);
      state.pickerSnapAnim = null;
    }
    pickerTrack.classList.remove("is-snapping");
    state.pickerScrollLock = false;
    if (!animate) {
      pickerTrack.scrollLeft = clamped;
      updatePickerCenters();
      return;
    }
    const start = pickerTrack.scrollLeft;
    if (Math.abs(start - clamped) < 2) {
      pickerTrack.scrollLeft = clamped;
      updatePickerCenters();
      return;
    }
    state.pickerScrollLock = true;
    pickerTrack.classList.add("is-snapping");
    const t0 = performance.now();
    const dur = Math.min(
      V1_CAROUSEL_SNAP_MS_MAX,
      Math.max(V1_CAROUSEL_SNAP_MS_MIN, 300 + Math.abs(clamped - start) * 0.55),
    );
    const frame = (now) => {
      const p = Math.min(1, (now - t0) / dur);
      pickerTrack.scrollLeft = start + (clamped - start) * easeInOut(p);
      updatePickerCenters();
      if (p < 1) {
        state.pickerSnapAnim = requestAnimationFrame(frame);
      } else {
        state.pickerSnapAnim = null;
        state.pickerScrollLock = false;
        pickerTrack.classList.remove("is-snapping");
        pickerTrack.scrollLeft = clamped;
        updatePickerCenters();
      }
    };
    state.pickerSnapAnim = requestAnimationFrame(frame);
  }

  function pickerFocusFromScroll() {
    const n = pickerTrack.querySelectorAll(".picker-card").length;
    if (!n) return 0;
    const mid = pickerTrack.scrollLeft + pickerTrack.clientWidth / 2;
    let best = 0;
    let bestDist = Infinity;
    for (let i = 0; i < n; i++) {
      const el = pickerCardAt(i);
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

  function pickerScrollAlignedToFocus() {
    const card = pickerCardAt(state.pickerFocus);
    if (!card) return false;
    const target = pickerSnapTarget(card);
    return Math.abs(pickerTrack.scrollLeft - target) < 2;
  }

  function updatePickerCenters() {
    const snapping = state.pickerScrollLock || !!state.pickerSnapAnim;
    const highlight =
      snapping || pickerScrollAlignedToFocus()
        ? state.pickerFocus
        : pickerFocusFromScroll();
    pickerTrack.querySelectorAll(".picker-card").forEach((card) => {
      const idx = Number(card.dataset.idx);
      card.classList.toggle("center", idx === highlight);
      const key = card.dataset.pickKey;
      card.classList.toggle("picked", state.pickerSelected.has(key));
    });
  }

  function refreshPickerRecBtn() {
    const on = state.pickerSelected.size > 0;
    pickerRec.disabled = !on;
    pickerRec.classList.toggle("enabled", on);
    pickerRecDisk.classList.toggle("on", on);
  }

  function buildPickerEveryoneCard(idx) {
    const card = document.createElement("button");
    card.type = "button";
    card.className = "msg-card user-card picker-card";
    card.dataset.idx = String(idx);
    card.dataset.pickKey = "__all__";
    const grad = cardGradStyle();
    card.style.background = grad.css;
    if (grad.light) card.classList.add("grad-light");
    const name = document.createElement("div");
    name.className = "user-card-name";
    name.textContent = "Everyone";
    card.appendChild(name);
    return card;
  }

  function sendRecipientSummary(ids, broadcast) {
    if (broadcast) return "Everyone";
    const names = ids.map((id) => {
      const u = state.hangoutUsers.find((x) => x.id === id);
      return u ? u.name : id;
    });
    if (names.length <= 3) return names.join(", ");
    return `${names[0]}, ${names[1]} +${names.length - 2}`;
  }

  function paintSendReceipt(phase) {
    const showRecipients = phase === "sent" || phase === "failed";
    const headlines = {
      finishing: "Finishing",
      sending: "Sending...",
      sent: "Sent",
      failed: "Couldn't send",
    };
    sendHeadline.textContent = headlines[phase] || "";
    sendHeadline.classList.toggle("failed", phase === "failed");
    sendCheck.classList.toggle("hidden", phase !== "sent");
    sendFaces.replaceChildren();
    sendNames.textContent = "";
    if (!showRecipients) return;
    if (state.sendBroadcast) {
      const mark = document.createElement("div");
      mark.className = "everyone-mark";
      mark.textContent = "*";
      sendFaces.appendChild(mark);
    } else {
      const show = state.sendRecipients.slice(0, 3);
      for (const id of show) {
        sendFaces.appendChild(portraitEl(id, 36));
      }
    }
    sendNames.textContent = sendRecipientSummary(
      state.sendRecipients,
      state.sendBroadcast,
    );
  }

  function tinyWavBlob() {
    const sampleRate = 16000;
    const numSamples = 800;
    const dataSize = numSamples * 2;
    const buf = new ArrayBuffer(44 + dataSize);
    const v = new DataView(buf);
    const writeStr = (off, s) => {
      for (let i = 0; i < s.length; i++) v.setUint8(off + i, s.charCodeAt(i));
    };
    writeStr(0, "RIFF");
    v.setUint32(4, 36 + dataSize, true);
    writeStr(8, "WAVE");
    writeStr(12, "fmt ");
    v.setUint32(16, 16, true);
    v.setUint16(20, 1, true);
    v.setUint16(22, 1, true);
    v.setUint32(24, sampleRate, true);
    v.setUint32(28, sampleRate * 2, true);
    v.setUint16(32, 2, true);
    v.setUint16(34, 16, true);
    writeStr(36, "data");
    v.setUint32(40, dataSize, true);
    return new Blob([buf], { type: "audio/wav" });
  }

  function delay(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  async function postOutboundMessage() {
    const fd = new FormData();
    fd.append("kind", "audio");
    fd.append("blob", tinyWavBlob(), "clip.wav");
    if (state.sendBroadcast) {
      fd.append("broadcast", "true");
    } else if (state.sendRecipients.length === 1) {
      fd.append("to_user_id", state.sendRecipients[0]);
      fd.append("broadcast", "false");
    } else {
      let ok = true;
      for (const id of state.sendRecipients) {
        const one = new FormData();
        one.append("kind", "audio");
        one.append("to_user_id", id);
        one.append("broadcast", "false");
        one.append("blob", tinyWavBlob(), "clip.wav");
        const res = await fetch(state.origin + "/v1/messages", {
          method: "POST",
          headers: sessionHeaders(),
          body: one,
        });
        if (!res.ok) {
          ok = false;
          break;
        }
        noteSendOrder(id);
      }
      return ok;
    }
    const res = await fetch(state.origin + "/v1/messages", {
      method: "POST",
      headers: sessionHeaders(),
      body: fd,
    });
    if (res.ok && state.sendBroadcast) {
      noteSendOrder("__all__");
    } else if (res.ok && state.sendRecipients[0]) {
      noteSendOrder(state.sendRecipients[0]);
    }
    return res.ok;
  }

  async function runOutboundSend() {
    if (!state.pickerSelected.size) return;
    state.sendBroadcast = state.pickerSelected.has("__all__");
    state.sendRecipients = state.sendBroadcast
      ? []
      : [...state.pickerSelected];
    setMode("send");
    paintSendReceipt("finishing");
    await delay(200);
    paintSendReceipt("sending");
    const ok = await postOutboundMessage();
    paintSendReceipt(ok ? "sent" : "failed");
    if (ok) {
      await reloadInbox();
    }
    await delay(V1_SEND_RECEIPT_MS);
    setMode("carousel");
  }

  function startPickerRecordStub() {
    runOutboundSend().catch((e) => {
      paintSendReceipt("failed");
      setToast(String(e), 2000);
      setTimeout(() => setMode("carousel"), V1_SEND_RECEIPT_MS);
    });
  }

  function paintPicker() {
    pickerTrack.replaceChildren();
    const users = pickerUsersOrdered();
    const items = [...users, { id: "__all__", name: "Everyone" }];
    state.pickerFocus = 0;
    items.forEach((u, i) => {
      const card =
        u.id === "__all__"
          ? buildPickerEveryoneCard(i)
          : (() => {
              const c = buildUserCard(u, i);
              c.classList.add("picker-card");
              c.dataset.pickKey = u.id;
              return c;
            })();
      card.addEventListener("click", () => {
        if (i !== state.pickerFocus) {
          state.pickerFocus = i;
          pickerSnapTo(pickerSnapTarget(card), true);
          return;
        }
        const key = card.dataset.pickKey;
        if (state.pickerSelected.has(key)) state.pickerSelected.delete(key);
        else state.pickerSelected.add(key);
        updatePickerCenters();
        refreshPickerRecBtn();
      });
      pickerTrack.appendChild(card);
    });
    const end = document.createElement("div");
    end.className = "carousel-end";
    pickerTrack.appendChild(end);
    requestAnimationFrame(() => {
      const focusCard = pickerCardAt(state.pickerFocus);
      if (focusCard) pickerSnapTo(pickerSnapTarget(focusCard), false);
      updatePickerCenters();
      refreshPickerRecBtn();
    });
  }

  function paint() {
    if (state.mode === "login") paintRoster();
    if (state.mode === "carousel" || state.mode === "settings") {
      paintCarousel({ scroll: true });
    }
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

  function tickIdleRelock() {
    if (state.privacyScreenOff) {
      return;
    }
    if (state.mode !== "carousel" && state.mode !== "settings") {
      return;
    }
    if (Date.now() - state.activityAt > IDLE_RELOCK_MS) {
      player.pause();
      state.playing = false;
      state.sessionUser = "";
      state.inbox = [];
      setupPanel.classList.remove("hidden");
      lcd.classList.add("busy");
      setMode("login");
      lineEl.textContent = "idle lock — enter PIN";
      state.activityAt = Date.now();
    }
  }

  function inboxAutoplayArmed() {
    if (!state.profile.autoplay_new) {
      return false;
    }
    if (!state.inbox.length) {
      return false;
    }
    if (state.focus !== state.inbox.length - 1) {
      return false;
    }
    return state.inbox.every((m) => m.read);
  }

  async function reloadInbox() {
    const armed = inboxAutoplayArmed();
    const inboxBefore = state.inbox.length;
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
      state.profile = {
        avatar_slot: data.profile.avatar_slot || 0,
        accent_hex: data.profile.accent_hex || ACCENTS[0],
        autoplay_new: !!data.profile.autoplay_new,
      };
      const u = state.hangoutUsers.find((x) => x.id === state.sessionUser);
      if (u) u.profile = state.profile;
    }
    paint();
    if (
      armed &&
      state.inbox.length > inboxBefore &&
      state.mode === "carousel"
    ) {
      state.focus = state.inbox.length - 1;
      scrollToFocus(true);
      await togglePlay();
    }
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
        autoplay_new: !!state.profile.autoplay_new,
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
    if (state.mode === "carousel" || state.mode === "settings") {
      paintCarousel({ scroll: false });
    }
  }

  function pickAccent(hex) {
    state.profile.accent_hex = hex.toUpperCase();
    const u = state.hangoutUsers.find((x) => x.id === state.sessionUser);
    if (u) {
      u.profile = { ...state.profile };
    }
    saveProfile().catch(() => {});
  }

  function pickAvatar(slot) {
    state.profile.avatar_slot = slot;
    const u = state.hangoutUsers.find((x) => x.id === state.sessionUser);
    if (u) {
      u.profile = { ...state.profile };
    }
    saveProfile().catch(() => {});
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
    noteLoginOrder(userId);
    state.activityAt = Date.now();
    state.dimmed = false;
    state.asleep = false;
    state.inbox = data.messages || [];
    state.focus = 0;
    if (data.profile) {
      state.profile = {
        avatar_slot: data.profile.avatar_slot || 0,
        accent_hex: data.profile.accent_hex || ACCENTS[0],
        autoplay_new: !!data.profile.autoplay_new,
      };
    }
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
    state.privacyScreenOff = false;
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
    if (state.modalKind) {
      closeModal();
      return;
    }
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
    if (state.mode === "send") {
      return;
    }
    if (state.mode === "carousel") {
      if (state.playing) return;
      if (Date.now() - state.carouselReadyAt < CIRCLE_DEBOUNCE_MS) return;
      setMode("picker");
    } else if (state.mode === "picker") {
      startPickerRecordStub();
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
    setFocus(activeFocus() + dir, true);
  }

  lcd.addEventListener("pointerdown", () => noteActivity());

  setInterval(tickSleepPolicy, 500);
  setInterval(tickIdleRelock, 500);

  $("login-btn").addEventListener("click", () => {
    login().catch((e) => {
      setupStatus.textContent = String(e);
    });
  });

  if (modalClose) modalClose.addEventListener("click", closeModal);
  if (modalDim) modalDim.addEventListener("click", closeModal);
  bootEl.addEventListener("click", shoulderPress);
  circleEl.addEventListener("click", circleTap);
  carouselTrack.addEventListener("click", (ev) => {
    const card = ev.target.closest(".msg-card");
    if (!card || card.disabled) return;
    const idx = Number(card.dataset.idx);
    if (Number.isNaN(idx)) return;
    if (state.mode === "settings") {
      if (idx === state.settingsFocus) {
        const kind = card.dataset.setting;
        if (kind) openModal(kind);
        return;
      }
      setFocus(idx, true);
      return;
    }
    if (idx === state.focus) return;
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

  let rosterScrollEndTimer = 0;
  rosterTrack.addEventListener("scroll", () => {
    updateRosterCenters();
    window.clearTimeout(rosterScrollEndTimer);
    rosterScrollEndTimer = window.setTimeout(() => {
      if (state.rosterSnapAnim) return;
      if (rosterScrollAlignedToFocus()) {
        updateRosterCenters();
        return;
      }
      state.rosterFocus = rosterFocusFromScroll();
      const card = rosterCardAt(state.rosterFocus);
      rosterSnapTo(rosterSnapTarget(card), true);
    }, 80);
  });

  let pickerScrollEndTimer = 0;
  pickerTrack.addEventListener("scroll", () => {
    updatePickerCenters();
    window.clearTimeout(pickerScrollEndTimer);
    pickerScrollEndTimer = window.setTimeout(() => {
      if (state.pickerSnapAnim) return;
      if (pickerScrollAlignedToFocus()) {
        updatePickerCenters();
        return;
      }
      state.pickerFocus = pickerFocusFromScroll();
      const card = pickerCardAt(state.pickerFocus);
      pickerSnapTo(pickerSnapTarget(card), true);
    }, 80);
  });

  pickerRec.addEventListener("click", () => startPickerRecordStub());

  carouselPlay.addEventListener("click", () => {
    if (carouselHeader.classList.contains("locked-off")) return;
    if (state.mode === "settings") return;
    togglePlay().catch(console.error);
  });

  carouselScrub.addEventListener("input", () => {
    if (carouselHeader.classList.contains("locked-off")) return;
    const m = currentMsg();
    if (!m) return;
    m.position_ms = Number(carouselScrub.value);
    if (state.playing) player.currentTime = m.position_ms / 1000;
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
    if (!on) {
      muteEl.setAttribute("aria-pressed", "true");
      mutePrivacyEngage();
    } else {
      muteEl.setAttribute("aria-pressed", "false");
      if (state.privacyScreenOff) {
        mutePrivacyWake();
      }
    }
  });

  function mutePrivacyEngage() {
    player.pause();
    state.playing = false;
    state.privacyScreenOff = true;
    state.asleep = false;
    state.dimmed = false;
    if (state.ws) state.ws.close();
    state.sessionUser = "";
    state.inbox = [];
    closeModal();
    setupPanel.classList.remove("hidden");
    lcd.classList.add("busy");
    setMode("login");
    lineEl.textContent = "muted — screen off";
    updateSleepVisuals();
  }

  function mutePrivacyWake() {
    state.privacyScreenOff = false;
    state.activityAt = Date.now();
    setupPanel.classList.remove("hidden");
    lcd.classList.add("busy");
    setMode("login");
    lineEl.textContent = "login to start";
    updateSleepVisuals();
  }

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
  setMode("login");
  /* Prefetch hangout so the twin can show user cards before PIN login. */
  state.origin = $("origin").value.replace(/\/$/, "");
  state.token = $("token").value.trim();
  loadHangout()
    .then(() => {
      if (state.mode === "login") paintRoster();
    })
    .catch(() => {
      lineEl.textContent = "hangout unreachable — start the v1 server";
    });
})();
