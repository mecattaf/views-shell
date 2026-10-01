// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { api, applyTheme, subscribe, NotInstalled, NotRunning, HOST } from "./lib/host.js";

const PAGES = ["shortcuts", "plugins", "compositor", "appearance", "notifications", "connection"];
const $ = (id) => document.getElementById(id);

function show(page) {
  if (!PAGES.includes(page)) page = "shortcuts";
  for (const p of PAGES) $(`page-${p}`).hidden = p !== page;
  for (const a of document.querySelectorAll("nav a")) {
    if (a.getAttribute("href") === `#${page}`) a.setAttribute("aria-current", "page");
    else a.removeAttribute("aria-current");
  }
}

function banner(text, error = false) {
  $("status").textContent = text;
  $("status").classList.toggle("error", error);
}

function cell(text, cls) {
  const td = document.createElement("td");
  td.textContent = text;
  if (cls) td.className = cls;
  return td;
}

async function renderBindings() {
  const rows = await api.bindings();
  const body = $("bindings");
  body.replaceChildren();
  for (const b of rows) {
    const tr = document.createElement("tr");
    const keys = document.createElement("td");
    const kbd = document.createElement("kbd");
    kbd.textContent = b.key;
    keys.append(kbd);
    const owner = b.locked ? `${b.file}:${b.line} (locked)` : `slot: ${b.plugin}`;
    tr.append(keys, cell(b.command), cell(owner, b.locked ? "locked" : ""));
    body.append(tr);
  }
}

async function refresh() {
  $("host-name").textContent = HOST;
  $("extension-id").textContent = chrome.runtime.id;
  try {
    const s = await api.status();
    applyTheme(s.theme);
    banner(`Connected to views-shell on ${s.machine}, compositor ${s.compositor}, theme ${s.theme?.name ?? "unknown"}.`);
    $("theme-name").textContent = s.theme?.name ?? "unknown";
    await renderBindings();
  } catch (e) {
    if (e instanceof NotInstalled) banner("The views-shell native messaging host is not installed for this browser. See Connection.", true);
    else if (e instanceof NotRunning) banner("views-shell is not running on this machine.", true);
    else banner(String(e), true);
  }
}

window.addEventListener("hashchange", () => show(location.hash.slice(1)));
show(location.hash.slice(1));
refresh().then(() => {
  try {
    subscribe((ev) => {
      if (ev.type === "bindings-changed" || ev.type === "reload") renderBindings();
      if (ev.type === "theme") applyTheme(ev.theme);
      if (ev.type === "open" && ev.page) location.hash = ev.page;
    });
  } catch (_) { /* not connected; the banner already says so */ }
});
