// Copyright 2026 The Agency Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// chromium/src/agency/signal/niri/niri_events.cc

#include "agency/signal/niri/niri_events.h"

namespace agency {
namespace niri {

const char* EventTagToWireKey(EventTag tag) {
  // A real switch (not an indexed table) so the enum and its wire strings can
  // never drift when the enum is reordered -- the compiler flags a missing case.
  switch (tag) {
    case EventTag::kWorkspacesChanged:
      return "WorkspacesChanged";
    case EventTag::kWindowsChanged:
      return "WindowsChanged";
    case EventTag::kWindowOpenedOrChanged:
      return "WindowOpenedOrChanged";
    case EventTag::kWindowClosed:
      return "WindowClosed";
    case EventTag::kWorkspaceActivated:
      return "WorkspaceActivated";
    case EventTag::kWindowFocusChanged:
      return "WindowFocusChanged";
    case EventTag::kWorkspaceUrgencyChanged:
      return "WorkspaceUrgencyChanged";
    case EventTag::kKeyboardLayoutsChanged:
      return "KeyboardLayoutsChanged";
    case EventTag::kOverviewOpenedOrClosed:
      return "OverviewOpenedOrClosed";
    case EventTag::kConfigLoaded:
      return "ConfigLoaded";
    case EventTag::kCastsChanged:
      return "CastsChanged";
    case EventTag::kUnknown:
      // Not a wire tag; there is no key to emit for the parser sentinel.
      return nullptr;
  }
  // All enumerators are handled above; a value outside the enum's range is
  // undefined behavior in the caller, not a runtime condition we model.
  return nullptr;
}

EventTag WireKeyToEventTag(const std::string& wire_key) {
  // Linear compare against the known 11. The set is tiny and fixed, so a
  // sorted-map/hash would be more machinery than the lookup warrants; an
  // unmatched key is the expected forward-compat path (a newer niri emitting a
  // 12th event) and resolves to kUnknown, never a crash.
  if (wire_key == "WorkspacesChanged") {
    return EventTag::kWorkspacesChanged;
  }
  if (wire_key == "WindowsChanged") {
    return EventTag::kWindowsChanged;
  }
  if (wire_key == "WindowOpenedOrChanged") {
    return EventTag::kWindowOpenedOrChanged;
  }
  if (wire_key == "WindowClosed") {
    return EventTag::kWindowClosed;
  }
  if (wire_key == "WorkspaceActivated") {
    return EventTag::kWorkspaceActivated;
  }
  if (wire_key == "WindowFocusChanged") {
    return EventTag::kWindowFocusChanged;
  }
  if (wire_key == "WorkspaceUrgencyChanged") {
    return EventTag::kWorkspaceUrgencyChanged;
  }
  if (wire_key == "KeyboardLayoutsChanged") {
    return EventTag::kKeyboardLayoutsChanged;
  }
  if (wire_key == "OverviewOpenedOrClosed") {
    return EventTag::kOverviewOpenedOrClosed;
  }
  if (wire_key == "ConfigLoaded") {
    return EventTag::kConfigLoaded;
  }
  if (wire_key == "CastsChanged") {
    return EventTag::kCastsChanged;
  }
  return EventTag::kUnknown;
}

// Out-of-line special members. These structs hold std::vector/std::string/
// std::optional-of-struct members, so the copy/move/dtor are non-trivial;
// defining them here (rather than inline in the header) keeps the header light
// and gives every translation unit one canonical instantiation.

Workspace::Workspace() = default;
Workspace::Workspace(const Workspace&) = default;
Workspace::Workspace(Workspace&&) noexcept = default;
Workspace& Workspace::operator=(const Workspace&) = default;
Workspace& Workspace::operator=(Workspace&&) noexcept = default;
Workspace::~Workspace() = default;

WindowLayout::WindowLayout() = default;
WindowLayout::WindowLayout(const WindowLayout&) = default;
WindowLayout& WindowLayout::operator=(const WindowLayout&) = default;
WindowLayout::~WindowLayout() = default;

Window::Window() = default;
Window::Window(const Window&) = default;
Window::Window(Window&&) noexcept = default;
Window& Window::operator=(const Window&) = default;
Window& Window::operator=(Window&&) noexcept = default;
Window::~Window() = default;

WorkspacesChanged::WorkspacesChanged() = default;
WorkspacesChanged::WorkspacesChanged(const WorkspacesChanged&) = default;
WorkspacesChanged::WorkspacesChanged(WorkspacesChanged&&) noexcept = default;
WorkspacesChanged& WorkspacesChanged::operator=(const WorkspacesChanged&) =
    default;
WorkspacesChanged& WorkspacesChanged::operator=(WorkspacesChanged&&) noexcept =
    default;
WorkspacesChanged::~WorkspacesChanged() = default;

WindowsChanged::WindowsChanged() = default;
WindowsChanged::WindowsChanged(const WindowsChanged&) = default;
WindowsChanged::WindowsChanged(WindowsChanged&&) noexcept = default;
WindowsChanged& WindowsChanged::operator=(const WindowsChanged&) = default;
WindowsChanged& WindowsChanged::operator=(WindowsChanged&&) noexcept = default;
WindowsChanged::~WindowsChanged() = default;

WindowOpenedOrChanged::WindowOpenedOrChanged() = default;
WindowOpenedOrChanged::WindowOpenedOrChanged(const WindowOpenedOrChanged&) =
    default;
WindowOpenedOrChanged::WindowOpenedOrChanged(
    WindowOpenedOrChanged&&) noexcept = default;
WindowOpenedOrChanged& WindowOpenedOrChanged::operator=(
    const WindowOpenedOrChanged&) = default;
WindowOpenedOrChanged& WindowOpenedOrChanged::operator=(
    WindowOpenedOrChanged&&) noexcept = default;
WindowOpenedOrChanged::~WindowOpenedOrChanged() = default;

KeyboardLayoutsChanged::KeyboardLayoutsChanged() = default;
KeyboardLayoutsChanged::KeyboardLayoutsChanged(const KeyboardLayoutsChanged&) =
    default;
KeyboardLayoutsChanged::KeyboardLayoutsChanged(
    KeyboardLayoutsChanged&&) noexcept = default;
KeyboardLayoutsChanged& KeyboardLayoutsChanged::operator=(
    const KeyboardLayoutsChanged&) = default;
KeyboardLayoutsChanged& KeyboardLayoutsChanged::operator=(
    KeyboardLayoutsChanged&&) noexcept = default;
KeyboardLayoutsChanged::~KeyboardLayoutsChanged() = default;

Event::Event() = default;
Event::Event(const Event&) = default;
Event::Event(Event&&) noexcept = default;
Event& Event::operator=(const Event&) = default;
Event& Event::operator=(Event&&) noexcept = default;
Event::~Event() = default;

}  // namespace niri
}  // namespace agency
