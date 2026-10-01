# The workspace strip

`views_shell --left-tabs` shows the compositor's workspaces as a column of
Chrome tabs on the left of a window, with a black content area to the right.
The tabs are Chrome's own Views code, copied into `shell/tabs/` and never
linked (rules R1 and R5). Every copied file is a row in
[`CHROME-PORT-LEDGER.md`](../CHROME-PORT-LEDGER.md).

## What was ported

From `chrome/browser/ui/views/tabs/` at 154.0.8037.92:

- `Tab`, the tab view: title, close button, hover glow, focus ring, hit-test
  mask, keyboard and mouse selection.
- `TabStyle` and `TabStyleViews`, which hold the tab metrics, the colour state
  machine (active, selected, inactive, each frame-active or not, each hovered or
  not) and the path every highlight and clip uses.
- `VerticalTabStyleViews`, the shape and fill Chrome 154 draws for its own
  vertical tabs: a rounded rectangle, filled when active, selected or hovered.
- `GlowHoverController`, `TabCloseButton`, `TabTitle`, `TabSlotView`, a
  trimmed `TabSlotController`, the close icon, and the subset of
  `GetLayoutConstant` these read.

Everything tied to a browser is cut: `TabStripModel`, `WebContents`, the
favicon and alert indicator, hover cards, dragging, groups, splits, metrics and
feature flags. The ledger lists each cut per file.

## Why the vertical layout is ours

Chrome's `TabStrip` lays tabs out horizontally, with overlapping folder shapes.
Chrome 154 also ships vertical tabs, but as a separate tab class built on its
tab-collection model, which is welded to `TabStripModel`. So
`shell/tabs/workspace_strip.{h,cc}` is views-shell code. It is the
`TabSlotController` for its tabs and stacks them top to bottom. It uses the
metrics of Chrome's vertical strip: 12 px side padding and top padding, a
30 px tab height (`kVerticalTabHeight`) and a 2 px gap. The ported `Tab` takes
an orientation so that it uses the vertical style and height.

One tab per workspace. The active workspace is the selected tab. A click, tap,
Return or Space on another tab selects it and runs the strip's callback. For
now the callback only logs the workspace, and it does not ask the compositor to
switch. The close button is ported but stays hidden: workspaces are not closed
from the strip.

## Where the workspaces come from

`shell/tabs/workspace_source.h` takes one snapshot per fetch:

- `static`: four demo workspaces, the first active.
- `niri`: `niri msg -j workspaces`, run on the thread pool and parsed with
  `base::JSONReader`. Workspaces are ordered by output, then index. A tab shows
  the workspace name, or else its index, and the focused workspace is active.

`--workspace-source=static|niri` picks one. Without it, the strip uses niri
when `NIRI_SOCKET` is set and the static list otherwise. Neither source follows
change events yet: that belongs to the compositor adapter. scroll is left for
later. Its i3-ipc client exists only on an unmerged branch.

## How colours map

The tab reads Chrome's tab colour ids. `shell/tabs/tab_color_id.h` declares
the 18 it uses, with Chrome's names, in a views-shell range that starts at
`ui::kUiColorsEnd`. `shell/tabs/tab_color_mixer.cc` is Chrome's
`material_tab_strip_color_mixer.cc`, cut to those ids, so every tab colour
resolves to a stock `kColorSys*` token:

| Tab id | Stock recipe |
|---|---|
| active tab background | `kColorSysBase` |
| inactive tab background (the strip itself) | `kColorSysHeader`, `kColorSysHeaderInactive` |
| hovered inactive tab | `kColorSysStateHeaderHover` |
| selected tab | `kColorSysStateHeaderSelect` over the inactive background |
| active tab text | `kColorSysOnSurface` |
| inactive tab text | `kColorSysOnSurfaceSecondary`, blended for minimum contrast |
| focus rings | `kColorFocusableBorderFocused`, picked for contrast |

`views_shell` registers the mixer with
`ColorProviderManager::AppendColorProviderInitializer` at startup. A theme
mixer appended after it (rule R17) recolours the strip through the `kColorSys*`
tokens, with no change to the tab code.
