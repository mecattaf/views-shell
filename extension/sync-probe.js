// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Storage sync probe (docs/storage-sync.md, procedure H1, open question Q1).
// Writes a timestamped record and the default user configuration into
// chrome.storage.sync, reads them back, shows getBytesInUse beside the quota
// constants, and logs every chrome.storage.onChanged event with its area.
// Classic script on purpose: no modules, no native messaging, no views-shell.

"use strict";

// BEGIN examples/config/default.json (kept identical to the file; tools/validate.py checks)
const DEFAULT_CONFIG = {
  "$schema": "../../schemas/config.schema.json",
  "schemaVersion": 1,
  "theme": { "name": "noir" },
  "plugins": {
    "views-shell.workspace-rail": {},
    "views-shell.network": {},
    "views-shell.qs-audio": {},
    "views-shell.qs-network": {},
    "views-shell.qs-power-footer": {},
    "example.music-scratchpad": {}
  },
  "bar": {
    "height": 32,
    "position": "top",
    "sections": {
      "left": [],
      "center": [],
      "right": ["example.music-scratchpad/button"]
    }
  },
  "keybindings": []
};
// END examples/config/default.json

const QUOTA_CONSTANTS = [
  "QUOTA_BYTES",
  "QUOTA_BYTES_PER_ITEM",
  "MAX_ITEMS",
  "MAX_WRITE_OPERATIONS_PER_HOUR",
  "MAX_WRITE_OPERATIONS_PER_MINUTE",
];

const $ = (id) => document.getElementById(id);

function setStatus(text, isError) {
  const banner = $("status");
  banner.textContent = text;
  banner.classList.toggle("error", Boolean(isError));
}

function lastError() {
  return chrome.runtime.lastError ? chrome.runtime.lastError.message : null;
}

function row(cells) {
  const tr = document.createElement("tr");
  for (const c of cells) {
    const td = document.createElement("td");
    td.textContent = c;
    tr.appendChild(td);
  }
  return tr;
}

function renderQuota() {
  const tbody = $("quota");
  tbody.textContent = "";
  for (const name of QUOTA_CONSTANTS) {
    tbody.appendChild(row([`chrome.storage.sync.${name}`, String(chrome.storage.sync[name])]));
  }
  chrome.storage.sync.getBytesInUse(null, (total) => {
    if (lastError()) {
      tbody.appendChild(row(["getBytesInUse(null)", lastError()]));
      return;
    }
    tbody.appendChild(row(["getBytesInUse(null) — total bytes in use", String(total)]));
    chrome.storage.sync.get(null, (items) => {
      for (const key of Object.keys(items).sort()) {
        chrome.storage.sync.getBytesInUse(key, (bytes) => {
          tbody.appendChild(row([`getBytesInUse("${key}")`, String(bytes)]));
        });
      }
    });
  });
}

function renderItems(items) {
  const tbody = $("items");
  tbody.textContent = "";
  const keys = Object.keys(items).sort();
  if (keys.length === 0) {
    tbody.appendChild(row(["(empty)", "", ""]));
    return;
  }
  for (const key of keys) {
    const value = JSON.stringify(items[key]);
    chrome.storage.sync.getBytesInUse(key, (bytes) => {
      tbody.appendChild(row([key, String(bytes), value.length > 300 ? value.slice(0, 300) + "…" : value]));
    });
  }
}

function readBack() {
  chrome.storage.sync.get(null, (items) => {
    const err = lastError();
    if (err) {
      setStatus(`Read failed: ${err}`, true);
      return;
    }
    renderItems(items);
    renderQuota();
    const probe = items.probe;
    setStatus(probe
      ? `Read back ${Object.keys(items).length} item(s); probe record ${probe.at} is present.`
      : `Read back ${Object.keys(items).length} item(s); no probe record yet.`);
  });
}

function write() {
  const record = {
    at: new Date().toISOString(),
    ms: Date.now(),
    note: "views-shell sync probe (docs/storage-sync.md, H1)",
  };
  chrome.storage.sync.set({ probe: record, config: DEFAULT_CONFIG }, () => {
    const err = lastError();
    if (err) {
      setStatus(`Write failed: ${err}`, true);
      return;
    }
    setStatus(`Wrote probe record ${record.at} and the default config. Watching for the echo on the other seat…`);
    readBack();
  });
}

function clear() {
  chrome.storage.sync.clear(() => {
    const err = lastError();
    setStatus(err ? `Clear failed: ${err}` : "Cleared the sync area.", Boolean(err));
    readBack();
  });
}

function logChange(changes, area) {
  const tbody = $("events");
  const placeholder = tbody.querySelector("td.locked");
  if (placeholder) {
    tbody.textContent = "";
  }
  const keys = Object.keys(changes).sort().join(", ");
  const detail = JSON.stringify(changes);
  tbody.appendChild(row([
    new Date().toISOString(),
    area,
    keys,
    detail.length > 300 ? detail.slice(0, 300) + "…" : detail,
  ]));
}

chrome.storage.onChanged.addListener(logChange);
$("write").addEventListener("click", write);
$("read").addEventListener("click", readBack);
$("clear").addEventListener("click", clear);

renderQuota();
readBack();
