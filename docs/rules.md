# The rules

These rules are fixed. A change that breaks one is wrong even when it works. Each
rule names how it is enforced, where it can be enforced. Rule numbers are stable:
a retired rule keeps its number.

## Surfaces

**R1. Views only on layer-shell.** Views C++ is drawn only on
`zwlr_layer_surface_v1` surfaces and on the `xdg_popup` children those surfaces own
(menus, bubbles, tooltips). views-shell never creates an `xdg_toplevel`. It never draws an
app window or a fullscreen app. No full-output overlay is drawn except an
allowlisted scrim.

Enforced three ways:
- The build forbids dependencies on `//chrome`, `//ash` and `//chromeos`
  (`assert_no_deps` on the executable; see also R24).
- The views-shell `ViewsDelegate` refuses any top-level `Widget` that has no registered
  `SurfaceSpec`. The check is a `CHECK`, not a log line.
- Every run gate asserts that the compositor's window list holds no views-shell window.

**R2. No terminal in Views, and no terminal reinvented.** The terminal is kitty
with herdr. views-shell does not build a terminal, in Views or anywhere else. A terminal
in a Chrome app window stays a later idea, until something clears the bar for
fonts, keyboard protocols, image protocols and idle cost.

**R3. Fullscreen belongs to someone else.** The browser, web apps and every views-shell
control page are Chrome tabs and windows. Nautilus and the GTK file chooser serve
files. views-shell serves no file selector and has no file manager, in Views or otherwise
(Tom, 2026-10-01: Google did not build one in Views, and they are rarely used).
Ash's terminal, Files app, browser and all of `ash/webui` are out.

**R4. The lock screen is not Views.** `ext-session-lock` is not layer-shell, so a
lock screen is an existing locker's job, never a views-shell surface. Whether the seats
bind a locker at all is Tom's call (swaylock is the candidate; swayidle already
turns the monitors off). `lock` is a verb, and `unlock` never is.

## Code

**R5. Views first; port Ash only where Views has no twin; never link Ash.** The
donor is stock Chromium Views C++: `ui/views`, `ui/color`, `ui/base`,
`ui/message_center`, `components/global_media_controls`, `device/bluetooth` and
`//dbus` all build on Linux and are linked as is. The views views-shell wants are the
least wm-coupled code in Ash, and most of them are thin subclasses of a stock
control, so the stock control wins. An Ash file is copied into `//views-shell` only when
(1) it has no `ash::Shell`, root-window, session or window-manager reference,
(2) stock Views has no equivalent within one subclass, and (3) a core or
first-party plugin surface needs it now. Each copy is recorded in
[`ASH-PORT-LEDGER.md`](../ASH-PORT-LEDGER.md) with its BSD-3 header kept. Ash, Exo
and the ChromeOS components assert `is_chromeos`; views-shell is an `is_linux` program
and never links them.

**R6. The seam law.** Keep everything below the upstream seam, and re-author only
what sits behind it. An Ash view that only draws is a candidate for a port; its
stock Views twin wins when one exists. Every Ash controller that mutates windows
becomes "compositor command, then observed echo". views-shell asks the compositor to act,
and redraws when the compositor reports the change.

**R7. Licences.** views-shell is BSD-3-Clause. Chromium headers stay on every file. MIT and
BSD sources (Chromium, aurade, noctalia, Omarchy) may be copied with their notices.
GPL and LGPL sources (niri among them) are read, never copied. Other projects'
licences are inspiration only.

**R8. Single writers.** The views-shell process is the only writer of window-manager
config slots and the only source of live change events for the CLI and the Chrome
extension. One writer sets GTK state through gsettings (Tom's `theme` script today;
a built-in theme plugin later). One owner holds each D-Bus bus connection.

**R9. Existing daemons only.** PipeWire and WirePlumber, NetworkManager, BlueZ,
UPower, power-profiles-daemon, logind, MPRIS players, compositor IPC and Wayland
protocols. views-shell builds only thin producers over them. Producers publish their own
snapshots with their own JSON Schemas; they do not copy the interface shapes Ash's
UI consumes.

**R10. One producer per domain, full snapshots.** Producers emit full snapshots, not
patches. A feature is dropped by not registering it.

## Plugins

**R11. Plugins supply data and handlers, never pixels.** The tiers are T0 built-in
C++, T1 declarative, T2 a JSON-RPC subprocess that sends a `ui` tree, and T3 a
JavaScript host later. Only T0 links Views. Third-party plugins carry no C++.
Every quick-settings item is a plugin: first-party quick-settings entries are T1,
written exactly as a third party would write them, and bind to T0 source plugins
that draw nothing.

**R12. Words.** A plugin is a views-shell shell module. An extension is only a Google
Chrome extension. See [`naming.md`](naming.md).

**R13. Minimal Lua in the core.** The core needs scroll's Lua for one signal (the end
of a `jump`) until scroll emits it over IPC (hook H1 in
[`scroll-fork-hooks.md`](scroll-fork-hooks.md)). A plugin may ship scroll Lua as
the declared capability `scroll.lua`, which is reviewed as a permission. Runtime
`LUA_EVAL` is never reachable from Chrome or through the native messaging host.

**R14. Capabilities, never compositor names.** A plugin declares what it needs as
capabilities. An entry whose required capability is missing is not registered.

**R25. The credential modal is core and is the only password field.** The
NetworkManager secret agent, the BlueZ agent and the polkit agent share one modal
in the core, on an overlay layer surface with exclusive keyboard, built from
`ui::DialogModel`. It is never a plugin. A `ui` tree has no password node, and
credentials never pass through a plugin.

## Chrome

**R15. Chrome stays stock.** One views-shell extension. No fork, no literal `views-shell://`, no
side panel, no content scripts, no overlay on the current tab, and no extension
theme. Chrome follows the theme through the `BrowserThemeColor` managed policy,
the mechanism that let Omarchy archive its Chromium micro-fork.

**R16. The bridge trusts only the extension.** The extension reaches views-shell through
Chrome native messaging. Chrome starts the host itself and enforces
`allowed_origins`, which names only the pinned views-shell extension id. Nothing listens
on a port, and there is no pairing. The host accepts typed verbs only and relays
them to the views-shell Unix socket. Raw compositor commands and Lua evaluation are never
exposed.

## Look

**R17. Stock components in the theme you pass.** Components, shapes, type and
motion stay stock Chromium Views. Colour comes from the theme Tom passes, in
exactly Omarchy's theme format (a theme directory with `colors.toml`), applied by
one `ColorMixer` appended last. `#000000` is the background of the `noir` theme,
not a rule; a light theme must work. No component code changes for colour. There
is no theme doctor and no icon fallback chain.

**R26. GTK themes are the user's.** views-shell never writes GTK CSS. The GTK theme is
whatever the theme directory names (`gtk.theme`); on Tom's seats that is MacTahoe,
which is non-negotiable. No libadwaita stylesheet is generated.

## Build

**R24. A Views program, not a browser.** The views-shell binary links no `//content`, no
Blink renderer and no V8. It is one process: Views, Aura's in-process window tree,
the compositor and an in-process viz host. Enforced by `assert_no_deps` on
`//content/*`, `//third_party/blink/renderer/*` and `//v8/*` beside the R1 guards.
Anything that wants HTML goes to the installed Chrome: an extension page in a tab,
or a Chrome `--app` window placed by the compositor. Never a web helper process
inside the shell.

## Process

**R18. No new clocks.** views-shell declares no timers. Chromium hops are run by hand.

**R19. Runtime isolation for tests.** Every test that launches a compositor, sources
a script fragment or touches a runtime directory runs under
`~/.local/bin/runtime-test -- <cmd>`. The harness also unsets `SCROLLSOCK`,
`SWAYSOCK`, `I3SOCK`, `NIRI_SOCKET` and `HYPRLAND_INSTANCE_SIGNATURE`, and sets
`HOME` and the XDG directories before any `dbus-run-session`, so a bus-activated
service can never write the live dconf. Never source a script fragment selected
by an unbounded text range.

**R20. Strict patches.** Patches apply with plain `git apply`, never a three-way
merge. Success is a literal token, never an exit code of zero alone.

**R21. No effort estimates.** Plans give order and dependencies. They never give
durations or schedules.

**R22. Unsandboxed, and no Flatpak.** scroll hides its privileged globals from
sandboxed clients, so views-shell runs unsandboxed.

**R23. One compositor per session.**

**R27. No release history before v1.** Tom daily-drives `main`. There are no tags,
no changelog and no version promises until v1. v1 is cut when Google ships a
Chrome release that Tom names as the signal to freeze; see
[`chromium-hop.md`](chromium-hop.md).
