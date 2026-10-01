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

## The vertical strip is Chrome's

Chrome has its own vertical tab strip, and 154 carries it. The code is in
`chrome/browser/ui/views/frame/vertical_tab_strip_region_view.*` (the region:
top and bottom buttons, resize, collapse, expand on hover),
`chrome/browser/ui/views/tabs/common/` (`TabStripView`,
`UnpinnedTabContainerView`, `TabView` and their layouts),
`chrome/browser/ui/views/tabs/vertical/` (the region's button containers and
scroll bar), `vertical_tab_style_views.*` (the tab's shape and fill) and
`chrome/browser/ui/tabs/vertical_tab_strip_state*` (the collapse state).
`TabStripOrientation` (`shared/tab_strip_types.h`) selects horizontal or
vertical throughout.

The views and the region are welded to `TabCollectionNode`,
`TabStripCollectionController`, `BrowserView` and prefs. Their painting and
layout are not, so those are what views-shell ports:

- `VerticalTabStyleViews` paints each tab.
- `TabViewVerticalLayout`, the layout of Chrome's vertical `TabView`, is
  installed on the ported `Tab`. It places the close button at the trailing
  edge and the title in the rest. It shows the close button on the active tab
  or on a hovered or focused tab.
- `UnpinnedTabContainerViewLayout`, cut to its vertical half, stacks the tabs.
  It uses a 12 px side padding, the 30 px `kVerticalTabHeight` and a 2 px gap.

`shell/tabs/workspace_strip.{h,cc}` is views-shell code where Chrome has
`VerticalTabStripRegionView`. It adds the region's 12 px vertical padding and
paints the frame colour behind the tabs. It holds one container with the
ported layout, and it is the `TabSlotController` for its tabs.

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
