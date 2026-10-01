// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Client for the views-shell native messaging host (see extension/README.md).
// Chrome starts `views-shell native-host` when this extension connects, and only
// because the host manifest lists this extension's id in allowed_origins.
// Nothing listens on a port and there is nothing to pair: installing the host
// manifest is the pairing. The host relays typed verbs to the views-shell process over
// its Unix socket.

export const HOST = "views-shell.host"; // placeholder; renamed with the product (docs/naming.md)

export class NotInstalled extends Error {}
export class NotRunning extends Error {}

let port = null;
let nextId = 1;
const pending = new Map();
const listeners = new Set();

function connect() {
  if (port) return port;
  port = chrome.runtime.connectNative(HOST);
  port.onMessage.addListener((msg) => {
    if (msg && msg.id !== undefined && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      if (msg.error) {
        reject(msg.error.code === "not-running" ? new NotRunning(msg.error.message) : new Error(msg.error.message));
      } else {
        resolve(msg.result ?? null);
      }
      return;
    }
    // Anything without an id is a push: bindings-changed, reload, config-error, wm, open, theme.
    for (const fn of listeners) fn(msg);
  });
  port.onDisconnect.addListener(() => {
    const why = chrome.runtime.lastError?.message || "disconnected";
    const err = /not found|not allowed|forbidden/i.test(why)
      ? new NotInstalled(`The views-shell native messaging host is not installed for this browser (${why})`)
      : new NotRunning(why);
    for (const { reject } of pending.values()) reject(err);
    pending.clear();
    port = null;
  });
  return port;
}

// One typed verb. The host refuses anything that is not in the contract.
function call(verb, params = {}) {
  return new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    try {
      connect().postMessage({ id, verb, params });
    } catch (e) {
      pending.delete(id);
      reject(new NotInstalled(String(e)));
    }
  });
}

export const api = {
  status: () => call("status"),
  bindings: () => call("bindings.list"),
  setBinding: (plugin, index, key) => call("bindings.set", { plugin, index, key }),
  plugins: () => call("plugins.list"),
  enable: (plugin) => call("plugins.enable", { plugin }),
  disable: (plugin) => call("plugins.disable", { plugin }),
  setConfig: (plugin, values) => call("plugins.config", { plugin, values }),
  commands: () => call("commands.list"),
  invoke: (plugin, command, args = {}) => call("commands.invoke", { plugin, command, args, source: "chrome" }),
  wm: () => call("wm"),
};

// Pushes arrive on the same port; keeping it open also keeps the service worker
// alive (Chrome 105+).
export function subscribe(onEvent) {
  listeners.add(onEvent);
  connect();
  return () => listeners.delete(onEvent);
}

// Paint a page in the theme views-shell is using. status.theme carries the resolved
// Omarchy keys the pages need; without it the pages follow the system scheme.
export function applyTheme(theme) {
  if (!theme) return;
  const root = document.documentElement.style;
  const set = (name, value) => value && root.setProperty(name, value);
  set("--ground", theme.background);
  set("--on-ground", theme.foreground);
  set("--subtle", theme.light_foreground || theme.muted);
  set("--outline", theme.muted);
  set("--primary", theme.accent);
  set("--alert", theme.red);
  document.documentElement.style.colorScheme = theme.mode === "light" ? "light" : "dark";
}
