"use strict";

const $ = (id) => document.getElementById(id);
const log = (msg) => {
  $("log").textContent = msg;
};

let adminToken = null;

function origin() {
  return $("origin").value.replace(/\/$/, "");
}

function authHeaders() {
  return { Authorization: `Bearer ${adminToken}` };
}

async function login() {
  const res = await fetch(`${origin()}/v1/admin/login`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      username: $("username").value.trim(),
      password: $("password").value,
    }),
  });
  if (!res.ok) {
    log(`Login failed (${res.status})`);
    return;
  }
  const data = await res.json();
  adminToken = data.token;
  $("login-panel").classList.add("hidden");
  $("admin-panel").classList.remove("hidden");
  log(`Signed in as ${data.user_id}`);
}

async function resetPin() {
  const pin = $("new-pin").value.trim();
  if (!pin) {
    log("Enter a new PIN");
    return;
  }
  const res = await fetch(`${origin()}/v1/admin/pin-reset`, {
    method: "POST",
    headers: { ...authHeaders(), "Content-Type": "application/json" },
    body: JSON.stringify({ user_id: $("reset-user").value, pin }),
  });
  const data = await res.json();
  if (!res.ok) {
    log(`PIN reset failed: ${JSON.stringify(data)}`);
    return;
  }
  log(`PIN for ${data.user_id} is now ${data.pin} (tell them out of band)`);
}

async function uploadWelcome() {
  const file = $("welcome-wav").files[0];
  if (!file) {
    log("Pick a WAV file");
    return;
  }
  const form = new FormData();
  form.append("blob", file, file.name);
  const res = await fetch(`${origin()}/v1/admin/welcome`, {
    method: "POST",
    headers: authHeaders(),
    body: form,
  });
  const data = await res.json();
  if (!res.ok) {
    log(`Welcome upload failed: ${JSON.stringify(data)}`);
    return;
  }
  log(`Welcome seeded for ${(data.seeded || []).length} users`);
}

async function sendAudio() {
  const file = $("send-wav").files[0];
  if (!file) {
    log("Pick a WAV to send");
    return;
  }
  const to = $("send-user").value;
  const form = new FormData();
  form.append("kind", "audio");
  form.append("blob", file, file.name);
  if (to === "broadcast") {
    form.append("broadcast", "true");
  } else {
    form.append("to_user_id", to);
  }
  const res = await fetch(`${origin()}/v1/admin/messages`, {
    method: "POST",
    headers: authHeaders(),
    body: form,
  });
  if (!res.ok) {
    const text = await res.text();
    log(`Send failed (${res.status}): ${text}`);
    return;
  }
  log(`Sent audio (${to})`);
}

$("login-btn").addEventListener("click", () => login().catch((e) => log(String(e))));
$("reset-btn").addEventListener("click", () => resetPin().catch((e) => log(String(e))));
$("welcome-btn").addEventListener("click", () => uploadWelcome().catch((e) => log(String(e))));
$("send-btn").addEventListener("click", () => sendAudio().catch((e) => log(String(e))));

const saved = localStorage.getItem("v1_origin");
if (saved) {
  $("origin").value = saved;
}
$("origin").addEventListener("change", () => {
  localStorage.setItem("v1_origin", $("origin").value);
});
