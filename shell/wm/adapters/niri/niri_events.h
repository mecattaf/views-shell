// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/signal/niri/niri_events.h
//
// The typed decode of niri's `EventStream` IPC protocol -- the SignalSource
// control seam. niri (the compositor the agency browser process lives under as
// a wlr-layer-shell client) exposes a line-delimited JSON socket at
// $NIRI_SOCKET: the client sends the JSON string `"EventStream"`, the server
// replies with a `{"Ok": ...}` handshake, and thereafter streams one JSON
// object per event, each a SINGLE-KEY tagged object whose key names the event.
//
// This header is the parser's target type-vocabulary. Every field and every one
// of the 11 tags below was captured live from niri 26.04 (git 2789) in the
// Wave-2 pre-flight spike (docs/SPIKE-RESULTS-wave2.md S2), then re-verified by
// signal/niri/proof/niri_proto_probe.py against the running socket -- these are
// observed shapes, not guessed ones. The struct layouts mirror niri's own
// `niri-ipc` Rust event enum (serde-tagged) so the JSON keys map 1:1.
//
// Design note: niri's protocol is state-diff, not full-snapshot. WorkspacesChanged
// / WindowsChanged carry a COMPLETE vector (a resync), while the per-object events
// (WindowOpenedOrChanged / WindowClosed / WorkspaceActivated / ...) are deltas the
// consumer folds into its retained model. The agency producer layer above this
// (the workspace/window/output producers) is what re-broadcasts a full-vector
// Snapshot per the ObserverRegistry contract; this header stays a faithful,
// lossless decode of the wire and does no folding of its own.

#ifndef AGENCY_SIGNAL_NIRI_NIRI_EVENTS_H_
#define AGENCY_SIGNAL_NIRI_NIRI_EVENTS_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace agency {
namespace niri {

// The 11 event tags of niri's EventStream, in the order documented by the
// niri-ipc schema. `kUnknown` is NOT a wire tag -- it is the parser's sentinel
// for a single-key object whose key is not one of the known 11 (a forward-compat
// niri version emitting a new event), so the dispatcher can drop-and-log rather
// than crash. Every real decode carries one of the first 11.
enum class EventTag {
  kWorkspacesChanged,
  kWindowsChanged,
  kWindowOpenedOrChanged,
  kWindowClosed,
  kWorkspaceActivated,
  kWindowFocusChanged,
  kWorkspaceUrgencyChanged,
  kKeyboardLayoutsChanged,
  kOverviewOpenedOrClosed,
  kConfigLoaded,
  kCastsChanged,
  kUnknown,
};

// Returns the exact wire key (e.g. "WorkspacesChanged") for a known tag, or
// nullptr for kUnknown. Backed by a real switch -- no table indexing by enum
// value, so reordering the enum can never silently mis-map a tag to a string.
const char* EventTagToWireKey(EventTag tag);

// Maps a single-key object's wire key back to its tag; returns kUnknown for any
// key not in the known 11. This is the parser's dispatch primitive.
EventTag WireKeyToEventTag(const std::string& wire_key);

// ---------------------------------------------------------------------------
// Payload structs. Field names and JSON keys are 1:1; optionals model niri's
// nullable fields (serde `Option<T>` -> JSON null). Numeric widths follow the
// niri-ipc types: object ids are u64, layout indices/sizes are the compositor's
// own i32/f64.
// ---------------------------------------------------------------------------

// A niri workspace. `name` is null unless the user named the workspace;
// `active_window_id` is null on an empty workspace.
struct Workspace {
  uint64_t id = 0;
  // 1-based index within the workspace's output column.
  uint8_t idx = 0;
  std::optional<std::string> name;
  std::optional<std::string> output;
  bool is_urgent = false;
  bool is_active = false;
  bool is_focused = false;
  std::optional<uint64_t> active_window_id;

  Workspace();
  Workspace(const Workspace&);
  Workspace(Workspace&&) noexcept;
  Workspace& operator=(const Workspace&);
  Workspace& operator=(Workspace&&) noexcept;
  ~Workspace();
};

// A window's layout sub-object as niri reports it. Every geometry field is
// nullable in the wire schema except the two the compositor always fills, so
// each is modeled optional to stay lossless.
struct WindowLayout {
  // [column, row] position in the scrolling layout (1-based).
  std::optional<std::pair<int32_t, int32_t>> pos_in_scrolling_layout;
  std::optional<std::pair<double, double>> tile_size;
  std::optional<std::pair<int32_t, int32_t>> window_size;
  std::optional<std::pair<double, double>> tile_pos_in_workspace_view;
  std::optional<std::pair<double, double>> window_offset_in_tile;

  WindowLayout();
  WindowLayout(const WindowLayout&);
  WindowLayout& operator=(const WindowLayout&);
  ~WindowLayout();
};

// A niri window (toplevel). `app_id`/`title` are nullable (a just-mapped window
// may lack either); `pid` is null for windows niri can't attribute to a process.
struct Window {
  uint64_t id = 0;
  std::optional<std::string> title;
  std::optional<std::string> app_id;
  std::optional<int32_t> pid;
  std::optional<uint64_t> workspace_id;
  bool is_focused = false;
  bool is_floating = false;
  bool is_urgent = false;
  std::optional<WindowLayout> layout;
  // niri's monotonic focus timestamp {secs, nanos}; null if never focused.
  std::optional<uint64_t> focus_timestamp_secs;
  std::optional<uint64_t> focus_timestamp_nanos;

  Window();
  Window(const Window&);
  Window(Window&&) noexcept;
  Window& operator=(const Window&);
  Window& operator=(Window&&) noexcept;
  ~Window();
};

// ---- the 11 event payloads ----

// Full resync of the workspace vector.
struct WorkspacesChanged {
  std::vector<Workspace> workspaces;

  WorkspacesChanged();
  WorkspacesChanged(const WorkspacesChanged&);
  WorkspacesChanged(WorkspacesChanged&&) noexcept;
  WorkspacesChanged& operator=(const WorkspacesChanged&);
  WorkspacesChanged& operator=(WorkspacesChanged&&) noexcept;
  ~WorkspacesChanged();
};

// Full resync of the window vector.
struct WindowsChanged {
  std::vector<Window> windows;

  WindowsChanged();
  WindowsChanged(const WindowsChanged&);
  WindowsChanged(WindowsChanged&&) noexcept;
  WindowsChanged& operator=(const WindowsChanged&);
  WindowsChanged& operator=(WindowsChanged&&) noexcept;
  ~WindowsChanged();
};

// A single window was opened or one of its tracked fields changed. Delta.
struct WindowOpenedOrChanged {
  Window window;

  WindowOpenedOrChanged();
  WindowOpenedOrChanged(const WindowOpenedOrChanged&);
  WindowOpenedOrChanged(WindowOpenedOrChanged&&) noexcept;
  WindowOpenedOrChanged& operator=(const WindowOpenedOrChanged&);
  WindowOpenedOrChanged& operator=(WindowOpenedOrChanged&&) noexcept;
  ~WindowOpenedOrChanged();
};

// A window with this id was destroyed.
struct WindowClosed {
  uint64_t id = 0;
};

// A workspace became active on its output. `focused_workspace_id` is null when
// the activation did not move keyboard focus (e.g. activation on a non-focused
// output).
struct WorkspaceActivated {
  uint64_t id = 0;
  bool focused = false;
};

// Keyboard focus moved to the window with this id, or to no window (null =
// nothing focused, e.g. all workspaces empty).
struct WindowFocusChanged {
  std::optional<uint64_t> id;
};

// A workspace's urgency flag toggled.
struct WorkspaceUrgencyChanged {
  uint64_t id = 0;
  bool urgent = false;
};

// The configured keyboard layouts changed (or the active one switched).
struct KeyboardLayoutsChanged {
  std::vector<std::string> names;
  // Index into `names` of the currently-active layout.
  uint8_t current_idx = 0;

  KeyboardLayoutsChanged();
  KeyboardLayoutsChanged(const KeyboardLayoutsChanged&);
  KeyboardLayoutsChanged(KeyboardLayoutsChanged&&) noexcept;
  KeyboardLayoutsChanged& operator=(const KeyboardLayoutsChanged&);
  KeyboardLayoutsChanged& operator=(KeyboardLayoutsChanged&&) noexcept;
  ~KeyboardLayoutsChanged();
};

// The overview (exposé) was opened or closed.
struct OverviewOpenedOrClosed {
  bool is_open = false;
};

// niri (re)loaded its config. `failed` is true if the reload hit a parse error
// and niri fell back to the previous config.
struct ConfigLoaded {
  bool failed = false;
};

// The set of active screencasts changed. We keep only the count of active
// casts; the per-cast detail (target window/output) is not part of the agency
// signal surface, so decoding it losslessly here would be dead weight.
struct CastsChanged {
  size_t active_cast_count = 0;
};

// ---------------------------------------------------------------------------
// Event -- the tagged union the parser produces. Exactly one payload is engaged,
// selected by `tag`. Modeled as separate optionals rather than a raw union so
// the non-trivial payload structs (vectors/strings) get proper C++ lifetime
// management; the invariant "the optional matching `tag` is engaged" is
// established by the parser and asserted by the accessors below.
// ---------------------------------------------------------------------------
struct Event {
  EventTag tag = EventTag::kUnknown;

  std::optional<WorkspacesChanged> workspaces_changed;
  std::optional<WindowsChanged> windows_changed;
  std::optional<WindowOpenedOrChanged> window_opened_or_changed;
  std::optional<WindowClosed> window_closed;
  std::optional<WorkspaceActivated> workspace_activated;
  std::optional<WindowFocusChanged> window_focus_changed;
  std::optional<WorkspaceUrgencyChanged> workspace_urgency_changed;
  std::optional<KeyboardLayoutsChanged> keyboard_layouts_changed;
  std::optional<OverviewOpenedOrClosed> overview_opened_or_closed;
  std::optional<ConfigLoaded> config_loaded;
  std::optional<CastsChanged> casts_changed;

  Event();
  Event(const Event&);
  Event(Event&&) noexcept;
  Event& operator=(const Event&);
  Event& operator=(Event&&) noexcept;
  ~Event();
};

}  // namespace niri
}  // namespace agency

#endif  // AGENCY_SIGNAL_NIRI_NIRI_EVENTS_H_
