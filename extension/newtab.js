// Copyright 2026 The views-shell Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The new-tab page in the active theme: a glance at the workspaces when views-shell
// answers, and an empty page in the theme's background when it does not.
import { api, applyTheme } from "./lib/host.js";

try {
  const [status, wm] = await Promise.all([api.status(), api.wm()]);
  applyTheme(status.theme);
  const box = document.getElementById("workspaces");
  for (const ws of wm.workspaces) {
    const el = document.createElement("span");
    el.className = "ws" + (ws.focused ? " focused" : "");
    el.textContent = ws.name;
    box.append(el);
  }
} catch (_) {
  // Host missing or views-shell not running: an empty page in the system scheme.
}
