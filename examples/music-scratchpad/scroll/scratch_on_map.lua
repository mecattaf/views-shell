-- Copyright 2026 The views-shell Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
--
-- example.music-scratchpad: loaded by a `lua` line in this plugin's scroll slot
-- file, so it re-runs after every reload (reload discards scroll's Lua state).
-- It runs inside the compositor, which is why scroll.lua is a reviewed permission.
-- The API shape follows scroll's TUTORIAL.md Lua example (view_map callback,
-- view_get_app_id, view_get_container, command). Not yet run.

local function on_map(view, _)
  if scroll.view_get_app_id(view) == "music" then
    local container = scroll.view_get_container(view)
    scroll.command(container, "move scratchpad")
  end
end

scroll.add_callback("view_map", on_map, nil)
