// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { api, subscribe } from "./lib/host.js";

const PAGES = ["shortcuts", "appearance", "plugins", "compositor", "notifications", "connection"];

// Open the views-shell control page, or focus the tab that already shows it.
async function openPage(page = "shortcuts") {
  const url = chrome.runtime.getURL(`options.html#${page}`);
  const contexts = await chrome.runtime.getContexts({ contextTypes: ["TAB"] });
  const existing = contexts.find((c) => c.documentUrl && c.documentUrl.startsWith(chrome.runtime.getURL("options.html")));
  if (existing && existing.tabId >= 0) {
    await chrome.tabs.update(existing.tabId, { active: true, url });
    if (existing.windowId >= 0) await chrome.windows.update(existing.windowId, { focused: true });
    return;
  }
  await chrome.tabs.create({ url });
}

chrome.commands.onCommand.addListener((command) => {
  if (command === "open-views-shell") openPage();
});

// `views-shell open <page>` on the CLI arrives as a push on the native port.
try {
  subscribe((ev) => {
    if (ev && ev.type === "open") openPage(ev.page);
  });
} catch (_) {
  // Host not installed: the CLI falls back to starting Chrome at the page.
}

// Omnibox: `views-shell <page>` opens a page; `views-shell <plugin-id>/<command>` invokes a
// command through a typed verb. Nothing else is reachable.
chrome.omnibox.setDefaultSuggestion({ description: "views-shell: a page (shortcuts, plugins, …) or a command id" });

chrome.omnibox.onInputChanged.addListener(async (text, suggest) => {
  const q = text.trim().toLowerCase();
  const pages = PAGES.filter((p) => p.startsWith(q)).map((p) => ({ content: p, description: `Open ${p}` }));
  let commands = [];
  try {
    const list = await api.commands();
    commands = list
      .filter((c) => `${c.plugin}/${c.id} ${c.title}`.toLowerCase().includes(q))
      .slice(0, 6)
      .map((c) => ({ content: `${c.plugin}/${c.id}`, description: escapeXml(c.title) }));
  } catch (_) {
    // Host missing or views-shell not running: offer pages only.
  }
  suggest([...pages, ...commands]);
});

chrome.omnibox.onInputEntered.addListener(async (text) => {
  const t = text.trim();
  if (PAGES.includes(t) || t === "") return openPage(t || "shortcuts");
  const [pluginId, command] = t.split("/");
  if (pluginId && command) {
    try {
      await api.invoke(pluginId, command);
    } catch (_) {
      openPage("connection");
    }
  }
});

function escapeXml(s) {
  return s.replace(/[<>&'"]/g, (c) => ({ "<": "&lt;", ">": "&gt;", "&": "&amp;", "'": "&apos;", '"': "&quot;" })[c]);
}
