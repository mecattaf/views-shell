# The workspace strip

`views_shell --left-tabs` shows the compositor's workspaces as a column of
Chrome tabs on a rail at the left edge of the output, a layer surface 220 px
wide with an exclusive zone of its width (the "left-tabs" row of
`shell/app/surface_spec.cc`).
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
Return or Space on another tab selects it and runs the strip's callback. What
that does to the strip is its `SelectionMode`: `kLocal` moves the active tab at
once (the one-shot sources), `kRequest` changes nothing and leaves the move to
the next `SetWorkspaces` (the compositor model, rule R6). The close button is
ported but stays hidden: workspaces are not closed from the strip.

## Where the workspaces come from

`--workspace-source=scroll|static|niri` picks one. Without the flag, the strip
uses scroll when `SCROLLSOCK`, `SWAYSOCK` or `I3SOCK` is set, else niri when
`NIRI_SOCKET` is set, else the static list.

`scroll` is the compositor adapter (`shell/wm/adapters/scroll`, i3-ipc) and
its `WmModel`, the same model the bar's workspace strip follows.
`shell/tabs/workspace_strip_model_binding.h` observes the model: every applied
snapshot rebuilds the strip (the workspace id, its name or else its id as the
title, the session's focused workspace as the active tab), and a selected tab
is a `WmModel::FocusWorkspace` request. The strip changes nothing on the click;
it moves when the compositor's echo arrives as the next snapshot (rule R6).
Without a socket the strip is empty and the program says so once.

`shell/tabs/workspace_source.h` is the other kind: one snapshot per fetch, no
change events.

- `static`: four demo workspaces, the first active.
- `niri`: `niri msg -j workspaces`, run on the thread pool and parsed with
  `base::JSONReader`. Workspaces are ordered by output, then index. A tab shows
  the workspace name, or else its index, and the focused workspace is active.
  A click moves the strip and logs the workspace; niri is not asked.

On the bench, `tools/bench/worker/left-tabs-click.sh` clicks a tab through the
headless scroll's seat (`scrollmsg seat - cursor set|press|release`) and
reports the request and the echo.

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
