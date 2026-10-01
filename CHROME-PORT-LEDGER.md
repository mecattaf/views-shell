# Chrome port ledger

Every file views-shell copies from `//chrome` is recorded here. The rule is the
one [`ASH-PORT-LEDGER.md`](ASH-PORT-LEDGER.md) applies to Ash (rule R5): copy
the Views file into the tree with its BSD-3 header, never link the directory it
came from. The executable keeps `assert_no_deps` on `//chrome/*`, and the
`tabs` source set carries the same guard.

## Format

Same columns as the Ash ledger. Every copied file keeps its original
"Copyright … The Chromium Authors" header unchanged, with a provenance line
below it: `// Ported to views-shell from <path> @ <tag>.` "Stock twin
considered" names the `ui/views` piece that could have stood in, and why the
port won or lost.

Upstream revision for every row: **154.0.8037.92** (commit
`334b65d254ccc35df4fca82706d1753227b01039`).

Common local changes, not repeated per row: everything moves into
`namespace views_shell`; include paths become `views_shell/tabs/…`; header
guards become `VIEWS_SHELL_TABS_…`.

## Rows

| # | Upstream path | Upstream revision | Header kept | views-shell path | Local changes | Stock twin considered | Status |
|---|---|---|---|---|---|---|---|
| C001 | `chrome/browser/ui/views/tabs/tab.{h,cc}` | 154.0.8037.92 | yes | `shell/tabs/tab.{h,cc}` | Removed: `tabs::TabHandle`, `TabInterface`, `TabDataObserver` and `tabs::TabData` (a two-field `TabData` of title and pinned replaces it), `TabIcon` (favicon, throbber, attention), `AlertIndicatorButton`, glic underline, hover cards (`HoverCardAnchorTarget`), freezing votes, `TabDragController` and every drag call, multi-selection (shift/ctrl click), keyboard reorder (`event_utils`), group and split state, `BrowserWindowInterface`, `BrowserView`, `BrowserFrameView`, theme-provider `SHOULD_FILL_BACKGROUND_TAB_COLOR`, ChromeOS OnTask lock, tab declutter flag, `kTabElementId`, `VIEW_ID_TAB`, user-action metrics, the title slide animation (it only ran when a favicon appeared). Added: the constructor takes a `TabStripOrientation` (Chrome's `Tab` is always horizontal); a vertical tab's preferred size is the strip's width by `kVerticalTabHeight`, and the close-button height gate compares against that height; `TabSlotController::CanCloseTab()` replaces the OnTask check; the title is the tooltip, since there are no hover cards; the Linux hover-card overlap carve-out in `MaybeUpdateHoverStatus` is gone with the hover cards. | `views::TabbedPane`'s `views::Tab`: a text tab with a stock highlight, not Chrome's shape, hover glow or close button. Lost. | ported |
| C002 | `chrome/browser/ui/tabs/tab_style.{h,cc}` | 154.0.8037.92 | yes | `shell/tabs/tab_style.{h,cc}` | colour ids from `tab_color_id.h`; `GetLayoutConstant` from C003; the `chrome/browser/ui/tabs/features.h` and `ui_features.h` includes dropped (only `features::IsRoundedIconsEnabled`, from `ui/base`, was used). | none: no stock class holds tab metrics or the tab colour state machine. | ported |
| C003 | `chrome/browser/ui/layout_constants.{h,cc}` | 154.0.8037.92 | yes | `shell/tabs/layout_constants.{h,cc}` | only the 14 constants the tab and the vertical strip read; values unchanged. | `views::LayoutProvider` distances: they are not Chrome's tab metrics. | ported |
| C004 | `chrome/browser/ui/views/tabs/tab_style_views.{h,cc}` | 154.0.8037.92 | yes | `shell/tabs/tab_style_views.{h,cc}` | `TabStyleViewDelegate` loses group, split, `BrowserFrameView` and `BrowserWindowInterface` methods; `Create()` builds only the vertical style and `CHECK`s the orientation. | none. | ported |
| C005 | `chrome/browser/ui/views/tabs/vertical_tab_style_views.{h,cc}` | 154.0.8037.92 | yes | `shell/tabs/vertical_tab_style_views.{h,cc}` | theme-image painting removed (`IDR_THEME_TOOLBAR`, the frame's custom background, `PaintTabBackgroundWithImages`, `GetCurrentActiveOpacity`): views-shell paints the colour fill only; the split-tab corner-radius term removed. | `views::RoundedRectBackground`: no hover blend, no selection states. | ported |
| C006 | `chrome/browser/ui/views/tabs/tab/glow_hover_controller.{h,cc}` | 154.0.8037.92 | yes | `shell/tabs/glow_hover_controller.{h,cc}` | none beyond the common changes. | `views::InkDrop` hover highlight: a different fade and opacity curve from Chrome's tab. | ported |
| C007 | `chrome/browser/ui/views/tabs/tab/tab_close_button.{h,cc}` | 154.0.8037.92 | yes | `shell/tabs/tab_close_button.{h,cc}` | accessible name is the literal "Close" (`IDS_ACCNAME_CLOSE` is in `//components/strings`, not packed); only the default icon (C014) is ported, so the `kRoundedIcons` branch is gone. | `views::CreateVectorImageButtonWithNativeTheme`: different size, hit-test mask and ink drop. | ported (hidden for workspaces: `CanCloseTab()` is false) |
| C008 | `chrome/browser/ui/views/tabs/tab/tab_title.{h,cc}` | 154.0.8037.92 | yes | `shell/tabs/tab_title.{h,cc}` | the default "Untitled" text (`CoreTabHelper`) dropped. | `views::Label` with the same settings: it is one, ported for its settings. | ported |
| C009 | `chrome/browser/ui/views/tabs/tab_slot_view.{h,cc}` | 154.0.8037.92 | yes | `shell/tabs/tab_slot_view.{h,cc}` | group and split ids removed. | `views::View`. | ported |
| C010 | `chrome/browser/ui/views/tabs/tab_slot_controller.h` | 154.0.8037.92 | yes | `shell/tabs/tab_slot_controller.h` | trimmed from 45 methods to 15: no selection model, drag, groups, splits, hover cards, context menu, throbber, separators or Browser; `CanCloseTab()` added. | none. | ported |
| C011 | `chrome/browser/ui/views/tabs/shared/tab_strip_types.h`, `chrome/browser/ui/tabs/tab_enums.h` | 154.0.8037.92 | yes | `shell/tabs/tab_strip_types.h` | only `TabStripOrientation` and `CloseTabSource`. | none. | ported |
| C012 | `chrome/browser/ui/views/tabs/tab_strip_layout_types.h` | 154.0.8037.92 | yes | `shell/tabs/tab_strip_layout_types.h` | only `TabSizeInfo`. | none. | ported |
| C013 | `chrome/browser/ui/color/chrome_color_id.h` | 154.0.8037.92 | yes | `shell/tabs/tab_color_id.h` | the 18 tab ids the port reads, a plain enum starting at `ui::kUiColorsEnd` instead of Chrome's macro list. | stock `ui/color` ids directly: the tab code reads a frame-active/inactive matrix that stock ids do not have. | ported |
| C014 | `chrome/app/vector_icons/close_tab_chrome_refresh_old.icon`, `vector_icons.{cc,h}.template` | 154.0.8037.92 | yes | `shell/tabs/vector_icons/` | templates wrap the icons in `namespace views_shell`; aggregated with the stock `//components/vector_icons/vector_icons.gni` template. | `views::kIcCloseIcon`: a different glyph. | ported |
| C015 | `chrome/browser/ui/color/material_tab_strip_color_mixer.{h,cc}` (and four focus-ring recipes from `chrome_color_mixer.cc`) | 154.0.8037.92 | yes | `shell/tabs/tab_color_mixer.{h,cc}` | only the C013 ids; the glass-frame branch and the `ShouldApplyChromeMaterialOverrides` gate removed (no custom Chrome theme exists here). | none: this is the mapping onto stock `kColorSys*`. | ported |

## Not ported

| Upstream path | Reason or stock replacement |
|---|---|
| `chrome/browser/ui/views/tabs/tab_strip.{h,cc}` | welded to `TabStripModel`, `TabContainer`, drag and groups. The vertical strip is views-shell's own (`shell/tabs/workspace_strip.{h,cc}`), using Chrome's vertical-strip metrics. |
| `chrome/browser/ui/views/tabs/horizontal_tab_style_views.{h,cc}` | the folder-tab shape needs adjacent horizontal tabs, separators and `BrowserFrameView`; the strip is vertical, and Chrome's own vertical tabs use C005. |
| `chrome/browser/ui/views/tabs/common/tab_view*`, `unpinned_tab_container_view_layout*`, `vertical/*` | Chrome's vertical tab view is a second class on `TabCollectionNode` and `TabStripCollectionController`. Its metrics (12 px side padding, 2 px gap, 30 px height) are used, its code is not. |
| `chrome/browser/ui/views/tabs/tab/tab_icon.{h,cc}` | favicons, throbbers and discard rings are WebContents state; a workspace has none. |
| `chrome/browser/ui/views/tabs/tab/alert_indicator_button.{h,cc}` | audio and capture alerts are WebContents state. |
| `chrome/browser/ui/views/tabs/tab_strip_controller.h`, `browser_tab_strip_controller.*` | the model interface for `TabStripModel`; `shell/tabs/workspace_source.h` is the workspace data source instead. |
| hover cards, `tab_context_menu_controller`, `tab_accessibility`, dragging | Browser features with no workspace meaning. |
