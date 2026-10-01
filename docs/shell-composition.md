# Shell composition: how the lifted components become the whole shell

views-shell is additive. Chromium's Views files are copied into `//views-shell` with
their BSD-3 headers, cut loose from `Browser`, `Profile`, `TabStripModel`,
`WebContents`, feature flags and metrics, and recorded in the port ledger. This
page says what each shell surface is made of: which lifted components compose
it, which stock Views controls fill the rest, which data feeds it (the `WmModel`
or a plugin source), and which `ui`-tree node types
([`../schemas/ui-tree.schema.json`](../schemas/ui-tree.schema.json)) map onto
which component, so that a JSON plugin can draw with them. It ends with the
schema additions this implies and the first three surfaces to build.

The component names, line counts and cut counts come from the 154.0.8037.92
sweep of the bench checkout (tabs, toolbar and frame, omnibox and location bar,
bubbles and dialogs, side panel, downloads and media, profiles and
notifications, bookmarks and extensions, the stock `ui/views` kit,
`ui/views/examples`, the reapable Ash directories, and colour, theme and icons).
A component is sized by lines and by its number of decoupling cuts, never by
time (rule R21). "Lift" means copy and decouple; "link" means the file builds on
`is_linux` and is linked as is (`ui/views`, `ui/color`, `ui/message_center`,
`components/global_media_controls`, `components/vector_icons`, `services/media_session`);
"stock" means a control from the linked kit. Every lift lands in
[`../ASH-PORT-LEDGER.md`](../ASH-PORT-LEDGER.md), which grows beyond its five rows
under the same policy (rule R5): the ledger records Chrome copies too, and the
"Views first" test still applies to each one.

The layers this page composes are architecture §4 (the kit), §6 (the framework:
`SurfaceSpec` registry, `WmModel`, command registry, source plugins), §7 (the
renderer) and §8 (the surfaces). Rule R1 fixes where any of it may draw: layer
surfaces and their `xdg_popup` children.

## 1. The foundations every surface stands on

These are shared. Each is lifted or linked once and used by every surface below.
Without them no surface composes, so they are step 0 of the order in §5.

### 1.1 Shims that replace Chrome's two headers

Almost every lifted control reads `chrome/browser/ui/layout_constants.h` (363+249
lines, feature-flag driven) and `chrome/browser/ui/color/chrome_color_id.h`
(1212 lines). Neither is copied. Two shims absorb the majority of the cuts
counted below, so a cut named "layout constants to shell constants" or "colour
ids to shell tokens" is the same cut each time:

| Shim | What it is | Source pattern |
|---|---|---|
| views-shell `LayoutProvider` | The views-shell subclass of `views::LayoutProvider` (stock, 535 lines) with an extension enum covering the few Chrome-only metrics the lifts use (`DISTANCE_RICH_HOVER_BUTTON_ICON_HORIZONTAL`, `DISTANCE_SIDE_PANEL_*`, the vertical-tab-strip and location-bar chip constants as fixed values) | `ChromeLayoutProvider`'s shape; `shell.toml [spacing] scale` applies here |
| `views_shell_color_id.h` | The views-shell colour ids, an X-macro enum starting at `ui::kUiColorsEnd + 0x8000`, holding only the blocks the lifts need: tab and vertical tab strip, tab group, toolbar, side panel, hover button, omnibox result and chip, status bubble, info strip | `ui/views/examples/examples_color_id.h` (177 lines, 2 cuts) and the `chrome_color_id.h` subset (1261 lines read, 3 cuts) |
| the theme pin mixer | One `ColorMixer` appended last through `ColorProviderManager::AppendColorProviderInitializer`, which sets `kColorSys*` from `colors.toml` ([`../style/theme-map.json`](../style/theme-map.json)) and then the views-shell ids above from `kColorSys*` roles. The lifted `tab_strip_color_mixer` and `material_side_panel_color_mixer` recipes (362 and 165 lines) run inside it | `examples_color_mixer.cc`; `chrome_color_provider_utils` contrast helpers (179 lines, 2 cuts) |
| views-shell `TypographyProvider` | Already planned (INVENTORY §2); the lifts' `chrome_typography.h` contexts map onto it | `ChromeTypographyProvider` |

Two facts from the colour sweep shape the pin mixer. `ui::AddColorMixers` never
calls `key.custom_theme->AddColorMixers`, so a theme supplier in the key does
nothing in a content-free process; the appended initializer is the only seam,
and it is the one views-shell already plans. And `features::kChromeDarkNeutrals26`
(off by default, `ui/base`) moves the unpinned dark neutrals to near black
(`#000000`, `#0B0B0D`, `#111214`, …), which the `noir` and `all-black` themes may
enable with `--enable-features` and no extra pins.

### 1.2 Icons and resources

| Piece | Status | Note |
|---|---|---|
| `ui/views/vector_icons` (61), `ui/views/window/vector_icons` (8), `ui/message_center/vector_icons` (14), `components/vector_icons` (508) | link | `kMenuOpenIcon` and `kMenuCloseCustomIcon`, the vertical strip's collapse toggle, are in `ui/views/vector_icons` |
| `chrome/app/vector_icons` subset (`tab`, `split_scene*`, `dock_to_*`, `*_panel_close`, `fullscreen`, `new_window`, `grid_view`, `view_list`, `logout`, `exit_to_app`, `speaker_group`, `timer`, …) | lift (data, 1 cut) | copied `.icon` files compiled by a views-shell `aggregate_vector_icons` target; the template in `//components/vector_icons/vector_icons.gni` is linkable; no Google-branded icon |
| `components/omnibox/browser/vector_icons` subset (calculator, translate, find_in_page, …) | lift (data, 1 cut) | launcher answer rows |
| `ash/resources/vector_icons` subset | ledger A040 | the only source of wifi, battery-level and brightness glyphs |
| `DynamicVectorIcon` runtime `.icon` loader (`ui/views/examples/vector_icon_viewer.cc`, 589 lines with `vector_example`, 2 cuts) | lift | resolves `icon` names a plugin ships as `.icon` data; needs `GFX_VECTOR_ICONS_UNSAFE` |
| `ResourceBundle` with `views-shell.pak` | link | `InitSharedInstanceWithPakPath`, as the examples do; no `chrome_100_percent.pak` |

### 1.3 Framework pieces the lifts assume

| Piece | Source | Lines / cuts | Used by |
|---|---|---|---|
| `ui::ActionItem` + `views::ActionViewController`, with a views-shell `ActionId` range at `actions::kActionsEnd + 0x8000` | stock; pattern from `ui/views/examples/actions_example` and `examples_action_id.h` (640, 3) | link | the command registry; every tile, bar button, menu item and launcher row is one `ActionItem` |
| `BrowserLayoutParams` (visual client area, leading and trailing exclusion areas) | `chrome/browser/ui/views/frame/layout/browser_view_layout_params.{h,cc}` | 249 / 0 | the bar and rail reserve corners and exclusion zones; the `SurfaceSpec` registry carries them |
| `ScrimView` | `frame/scrim_view.{h,cc}` | 67 / 0 | the one allowlisted scrim (R1): credential modal, launcher |
| `ShadowFrameView` | `frame/shadow_frame_view.{h,cc}` | 168 / 1 | elevation for the rail flyout, quick settings, notifications, launcher, OSD |
| `WebUIBubbleReopenSuppressor` (renamed `BubbleReopenSuppressor`) | `bubble/webui_bubble_reopen_suppressor.{h,cc}` | 188 / 0 | every bar button that toggles a popover |
| `PageSwitcherView` + `SubpageView` | `controls/page_switcher_view.*`, `controls/subpage_view.*` | 79 / 0 and 214 / 2 | the page stack of quick settings, the launcher categories, the notification centre |
| `HoverButton` + `HoverButtonController` and `RichHoverButton` | `controls/hover_button*`, `controls/rich_hover_button.*`, `accessibility/non_accessible_image_view.*` | 771 / 3 and 534 / 3 | the generic list row: quick settings detail lists, launcher rows, user menu, device pickers |
| `RichControlsContainerView` | `controls/rich_controls_container_view.*` | 263 / 4 | toggle rows |
| `RichRadioButton`, `ExpandableContainerView` | `extensions/rich_radio_button.*`, `extensions/expandable_container_view.*` | 152 / 0 and 163 / 1 | pickers; details disclosure |
| `ToolbarButton` + `ToolbarInkDropUtil` | `toolbar/toolbar_button.*`, `toolbar/toolbar_ink_drop_util.*` | 1444 / 5 | the base of every bar and rail button |
| `TabCollectionAnimatingLayoutManager` | `tabs/common/tab_collection_animating_layout_manager.*` | 1074 / 0 | every animated list: rail, workspace strip, notification stack, launcher results, tiles |
| `RoundedScrollBar` | `tabs/shared/rounded_scroll_bar.*` | 242 / 0 | the thin overlay scrollbar beside stock `OverlayScrollBar` |
| `OmniboxMouseEnterExitHandler` | `omnibox/omnibox_mouse_enter_exit_handler.*` | 81 / 0 | hover state on rows with inline buttons |
| `JsonViewBuilder` | `ui/views/examples/json_view_builder.*` and its schema and unittest | 3461 / 6 | the engine of the ui-tree renderer (answers P9: copy); the ui-tree vocabulary is its front end, the metadata fall-through is closed, colour literals are refused, backgrounds become `ColorId`-backed |
| `message_center` model and views | `ui/message_center` | link (10,685 lines, 0 cuts) | notifications |
| `global_media_controls`, `media_message_center`, `services/media_session` | `components/…`, `services/media_session` | link (about 12,000 lines, 0 cuts) | media |

### 1.4 Data

Every surface reads one of three feeds and writes through one seam:

- **`WmModel`** (architecture §6): outputs, workspaces, windows, focus, fullscreen,
  urgency, MRU, keyed on the compositor's ids. It diffs snapshots into children
  added, removed and moved, which is exactly the callback shape
  `RootTabCollectionNode` fans out, so the rail's node tree is retyped onto it.
- **Source plugins** (T0 producers): `views-shell.audio`, `.network`, `.bluetooth`,
  `.power`, `.brightness`, `.session`, `.media`, `.notifications`, the tray, polkit
  and the launcher index, each a full snapshot with its own JSON Schema.
- **The settings store and the theme service**: `views-shell.json`, `shell.toml`
  sections, the theme pin mixer and `OnThemeChanged()`.
- **Writes** go through the command registry to a compositor adapter or a source
  plugin's commands, and the surface redraws on the observed echo (R6). No
  lifted controller keeps the Chrome path where it mutated a model directly.

## 2. The surfaces

Each surface lists its lifted set (name, lines, cuts), the stock controls that fill
the rest, its data, and the ui-tree nodes a plugin can draw there. Surface specs
(layer, anchors, zone, keyboard) are registry rows; the `kind` column follows the
plugin manifest's `contributes.surfaces`.

### 2.1 The bar

A `bar` surface on the top layer, anchored to one edge, with an exclusive zone (SPEC
C4.2 draws its first version at 32 px). The recipe is `ToolbarView::InitLayout`
(copied as a recipe, never as a class): an `AccessiblePaneView` with a horizontal
`FlexLayout`, cross-axis centre, collapsed margins and a default `kMarginsKey`,
three groups (start, centre, end) and an overflow controller for narrow outputs.

| Lifted | Lines / cuts | Draws in the bar |
|---|---|---|
| `ToolbarButton` + ink drop util (§1.3) | 1444 / 5 | every icon button: launcher, tray icons, media, notifications bell; long-press and right-click menus |
| `ToolbarChipButton` | 150 / 1 | pills: clock, workspace name, battery percentage; two chips butt together on a flat edge |
| `ToolbarDivider` | 43 / 1 | rounded group separators |
| `ToolbarIconContainerView` | 447 / 2 | the tray group: `AnimatingLayoutManager` over `FlexLayout`, a highlight border while any child is hovered |
| `PinnedToolbarActionsContainerLayout` | 217 / 0 | tray overflow by priority (high, medium, low) |
| `PinnedToolbarButtonStatusIndicator` | 162 / 0 | the engaged dot under a button (panel open, recording) |
| `ToolbarController` + `OverflowMenu` + `OverflowButton` | 1707 / 6 | the chevron that lists what `FlexLayout` dropped; the shell passes its own element list |
| `AppMenuButton` + `AppMenuControl` | 365 / 4 | the leading menu button, over a `ui::MenuModel` |
| `IconLabelBubbleView` + two layout strategies + `location_bar_util` | 1989 / 3 | status chips that flash a label then collapse to the icon: recording, screen share, mic in use, keyboard layout, network |
| `OmniboxChipButton` | 336 / 2 | a request chip (portal or polkit request pending, update ready) |
| `PermissionChipView` + `MultiImageContainer` and `PermissionDashboardView` | 1011 / 6 and 324 / 2 | the privacy cluster: an always-on in-use indicator beside a transient request chip with the painted arc divider; the chip state machine (collapse after 4 s, dismiss timer, hover restarts) is rewritten against the source plugin, never lifted |
| `LocationBarLayout` | 249 / 0 | the alternative packer: left and right decorations around a central element, auto-collapsing low-priority modules when the bar is narrow |
| `DownloadProgressRing` (from `download_toolbar_ui_controller.cc:107-232`) | 126 / 2 | a four-state ring around an icon: print queue, speech queue, model borrow, file transfer |
| `BadgedProfilePhoto` | 203 / 2 | the user avatar with a status badge |
| `BubbleReopenSuppressor` (§1.3) | 188 / 0 | every popover toggle |
| `StatusBubbleViews` (optional) | 1334 / 4 | an edge-attached status pill ("copied", a hover hint) that hops away from the pointer |
| horizontal workspace strip (optional, when the rail is hidden): `TabStripView` in `kHorizontal`, the horizontal extras (`TabScrollButtonContainer`, overflow indicator, closing helper) and `TabStripLayout` width math | 889 / 2 and 400 / 0 on top of the rail's set | workspace pills in the bar |

Patterns copied by hand, not lifted: `BrowserRootView::OnMouseWheel` (remainder
accumulation against `kWheelDelta`) for scroll-to-switch-workspace over the bar;
`AvatarToolbarButton::AnimateTextChange` (about 45 lines) for a chip that briefly
expands with text; `ReloadButton::ChangeMode` hover hysteresis for a two-state
toggle.

Stock fill: `views::Label` (clock, title), `ImageButton`, `MenuButton`,
`Separator`, `FlexLayoutView`, `AnimatingLayoutManager`, `Badge`, `DotIndicator`,
`BubbleDialogDelegateView` for every popover (an `xdg_popup` of the bar surface),
`BubbleSlideAnimator` to slide one open popover between bar items,
`MenuRunner` for menus.

Data: `WmModel` (focused window title and app id, workspaces, outputs, urgency);
source plugins for audio, network, bluetooth, power, session, media,
notification count and the tray; `base::Time` for the clock; the privacy cluster
reads the audio and session sources (camera, mic, screencast in use).

ui-tree nodes (through the bar slot, schema addition S1): `row` → `FlexLayoutView`;
`label` → `Label`; `icon` → `ImageView`; `iconButton` → `ToolbarButton` icon-only;
`button` → `ToolbarChipButton`; `chip` (S2) → `IconLabelBubbleView`; `badge`, `dot`
→ stock; `progress` with `shape: ring` → `DownloadProgressRing`; `separator` →
`ToolbarDivider`; `spacer` → flex spacer. A bar module's popover is a plugin
`surface` of kind `panel` anchored to the module.

### 2.2 The workspace rail on the left, on Chrome's vertical tab strip

The hero surface (architecture §8). Two surfaces: `rail` (kind `bar`, left edge,
56 DIP wide with a 56 DIP exclusive zone, keyboard none) and `flyout` (kind
`overlay`, anchored to the rail, no zone, keyboard on demand). Chrome's region
view collapses and expands within one window; here the collapsed form lives on
the rail surface and expand-on-hover draws into the flyout surface, so the
compositor never re-tiles on hover. The state controller's `kCollapsed`,
`kCollapsing` and `kExpanded` states drive which surface is mapped; the width
runtime setter of the `SurfaceSpec` registry replaces the resize of a browser
window.

The vertical strip at 154.0.8037.92 is three layers, and all three are lifted:

**Layer A, the rail container and its state.**

| Lifted | Lines / cuts | Role |
|---|---|---|
| `VerticalTabStripRegionView` + `ExpandOnHoverLock` + `TabStripAnimations` | 1683 / 12 | the column: vertical `FlexLayout` of header, separator, strip and footer; `ResizeArea` on the trailing edge (126 to 400 DIP uncollapsed, 240 default, 15 px snap); expand-on-hover after 350 ms or on the velocity heuristic; lock types `kForceCollapse`, `kKeepCurrentState`, `kKeepExpanded` so a menu, a drag or the launcher keeps the flyout open. `BrowserAnimationController` becomes `gfx::SlideAnimation` with the lifted timings (expand 400 ms, collapse 350 ms, hover 250 and 200 ms, `EASE_IN_OUT_EMPHASIZED`) |
| `VerticalTabStripStateController` + state struct | 792 / 7 | the rail model: enabled, collapse state, uncollapsed width, expand-on-hover, callbacks; prefs become the settings store (`collapsedWidth`, `expandedWidth`, `expandOnHover` in the rail manifest) |
| `VerticalTabStripTopContainer` + `TopContainerButton` | 763 / 7 | the header: collapse toggle (`kMenuOpenIcon`/`kMenuCloseCustomIcon`), launcher and search buttons; stacks vertically when collapsed; reserves a caption exclusion rectangle (a `BrowserLayoutParams` exclusion area) |
| `VerticalTabStripBottomContainer` | 168 / 3 | the footer: new-workspace button, quick settings entry |
| `TabStripFlatEdgeButton` and `TabStripComboButton` | 466 / 3 and 669 / 6 | segmented buttons that reflow between orientations |
| `RoundedScrollBar` + `VerticalTabStripScrollBar` | 330 / 1 | the strip's scrollbar |
| `VerticalTabStripFocusSwipeController` | 219 / 1 | two-finger swipe on the rail rotates the focused workspace |
| rail frame chrome: `ShadowFrameView`, `VerticalTabStripBackgroundBlurBackdrop` (without blur, by choice), `CustomCornersBackground` | 956 / 3 | the flyout's elevation 4 shadow and the rounded corner where the rail meets the bar |

**Layer B, the orientation-agnostic tree in `tabs/common/`, retyped onto the `WmModel`.**

| Lifted | Lines / cuts | Role |
|---|---|---|
| `TabCollectionNode` + `RootTabCollectionNode` + `TabStripCollectionController` | 2056 / 4 | the reconciler. `tabs::TabCollection` includes `tab_interface.h`, which includes `//content`, so the node tree is retyped onto a shell `WorkspaceNode` variant: `TABSTRIP` = one output, `GROUP` = a workspace, `TAB` = a window, `SPLIT` = a tiled pair or a scroll column, `PINNED` = pinned apps. The root observes the `WmModel` diff instead of `TabStripModelObserver`; the controller becomes a thin command surface over `focus-workspace`, `focus-window`, `rename-workspace`, `move-window` |
| `TabStripView` + `TabStripViewLayout` | 1349 / 5 | pinned grid above the window list, separator when collapsed, scroll-into-view of the active row |
| `PinnedTabContainerView` + `UnpinnedTabContainerView` + layouts + `tab_strip_layout_utils` | 1802 / 4 | the pinned-apps grid (32 px) and the window list (30 px rows) |
| `TabCollectionAnimatingLayoutManager` (§1.3) | 1074 / 0 | add, remove, reparent, drag snap |
| `TabView` + vertical layout + `TabStyleViews` + `VerticalTabStyleViews` + `TabStyle` | 2362 / 8 (3328 with `HorizontalTabStyleViews`, taken only for the bar's strip) | a window row: favicon with 8 px inset, title, alert indicator, close button on hover; active, selected (75 % opacity) and hover fills from the lifted colour ids; `TabData` becomes a shell window-data struct |
| `TabIcon` | 980 / 6 | the app icon with a throbber (an app starting), an attention dot (urgency) and a dotted ring (suspended) |
| `AlertIndicatorButton` | 709 / 5 | camera, mic, screencast and audio marks on a row, click to mute |
| `TabCloseButton` + `GlowHoverController` + `TabTitle` | 512 / 2 | the row leaves |
| `TabGroupView` + header + line + layout + `tab_group_style` | 2541 / 6 | a workspace as a collapsible coloured group: header chip with the name, a coloured leading edge line; the editor bubble becomes the rename-and-colour bubble below |
| `SplitTabView` | 484 / 3 | two tiled windows as one row (scroll's columns with `showScrollColumns`) |
| `DraggedTabsContainer` + `TabDragScrollHandler` + `TabDragTarget` | 1266 / 3 | reordering windows and workspaces in the rail |
| `TabDragHandler` + `TabLinkDropHandler` | 1470 / 5 | the drag session glue, rewritten against the `WmModel` (as cheap as lifting) |
| `DropArrow` | 206 / 2 | the drop indicator |
| shared types (`TabStripOrientation`, `EndDragReason`) | 279 / 1 | |
| tab hover card family (`TabHoverCardBubbleView`, `Fade*`, `FilenameElider`, anchor target, controller) | 3714 / 6 | the preview card beside a row: title, app id, and a screencopy thumbnail when the adapter declares the capability; also a workspace preview listing its windows |
| tab group editor bubble + `ColorPickerView` | 2181 / 8 (the bubble is rewritten small; `ColorPickerView` lifts with 0 model coupling) | rename a workspace and pick its colour |
| `tab_strip_color_mixer` + `material_tab_strip_color_mixer`, `tab_group_color_ids.h` | 362 / 5 and 78 / 0 | row colours; the nine group hues as the fallback palette when a theme has no hue keys |

Skipped on purpose: `TabDragController` and the cross-window drag (4821 lines; a
window move is the compositor's job), the legacy horizontal stack (15,489 lines;
pre-unification), the action container, glic, tab search (WebUI), the context
menu controller (stock `MenuRunner` over `contributes.menus` locations
`rail/workspace` and `rail/window`).

Stock fill: `AccessiblePaneView`, `ResizeArea`, `FlexLayout`, `Separator`,
`ScrollView`, `MenuRunner`, `BubbleDialogDelegateView` (hover card and editor
bubble as `xdg_popup` children of the rail or flyout surface).

Data: the `WmModel` only. Every click is a command, and the row redraws on the
compositor's echo (SPEC C5.2 proves the echo on scroll). Rename and move are
gated on adapter capabilities (`compositor.workspaces.rename`,
`compositor.windows.move-to-workspace`); a missing capability hides the menu
item and the drag target.

ui-tree nodes: the rail is T0 and draws its own rows. Plugins reach it through
`contributes.menus` (rail/workspace, rail/window) and commands. Two schema
additions let other surfaces reuse its rows: `listItem` gains `highlight` and
`actions` (S3, S4), and `repeat` gains `reorderAction` (S6) so the renderer can
put `DraggedTabsContainer` under a plugin list.

### 2.3 The launcher

An `overlay` surface, centred, keyboard exclusive, with the `ScrimView` behind
it. It is one card: a query field on top, results below, in the elevated rounded
card the omnibox draws (`views::BubbleBorder(STANDARD_SHADOW)` with
`set_md_shadow_elevation(16)` and `ShapeContextTokens::kOmniboxExpandedRadius`,
stock; `RoundedOmniboxResultsFrame` is not lifted).

| Lifted | Lines / cuts | Role |
|---|---|---|
| `OmniboxTextView` | 367 / 3 | result title and subtitle with fuzzy-match bold: the A041 ranker's hit ranges become `(offset, style)` spans |
| `OmniboxMatchCellView` | 817 / 4 | the 40 DIP row cell: icon column, contents, separator, description with shared-width elision |
| `OmniboxResultView` (+ selection indicator, hover-revealed remove button) | 1011 / 6 | one selectable row, `kListBoxOption` accessibility |
| `OmniboxRowView` + `OmniboxHeaderView` | 326 / 3 | section headers: Apps, Windows, Commands, Files |
| `OmniboxSuggestionButtonRowView` | 738 / 6 | action pills under a row (Open, Open in new window, Move to workspace N, Close) bound to `ActionItem`s |
| `OmniboxPopupViewViews` (list controller only, never its `PopupWidget`) | 1306 / 8 | the list: builds rows from a `LauncherModel`, keeps active-descendant and controls ids on the textfield, fades by layer opacity |
| `OmniboxRowGroupedView` | 147 / 2 | the "more results" or answer section reveal |
| `OmniboxLocalAnswerHeaderView` | 111 / 2 | a throbber and status line while the utility model streams an answer |
| `OmniboxMouseEnterExitHandler` (§1.3) | 81 / 0 | |
| `omnibox_theme.h` row-state table | 31 / 1 | hovered 0.10, selected 0.16 opacities on the views-shell ids |
| `SelectedKeywordView` | 329 / 4 | the scope chip ("Windows", "Calc", "Files", "Run") entered with Tab, from `picker-provider` prefixes |
| `SearchBoxViewBase` (Ash) or `FindBarView` | 1268 / 5 or 811 / 7 | the query row with a leading icon, ghost-text autocomplete and a clear button; one of the two, the Ash box when ghost text is wanted |
| `string_matching` | ledger A041 | ranking |
| `ScrimView`, `ShadowFrameView` (§1.3) | | |
| `ScrollableAppsGridView` + `AppListItemView` family (Ash, optional, later) | 10,931 / 10 | an app grid with drag-to-reorder; before it exists the grid is a `TableLayoutView` of `LabelButton`s |

The one cut that unblocks the whole row stack is a `LauncherMatch` struct
(contents, classification spans, description, icon `ImageModel`, group header,
actions) plus a `LauncherListDelegate` in place of `OmniboxController` and
`OmniboxEditModel`. `OmniboxViewViews` is not lifted: the query field is a stock
`views::Textfield`.

Stock fill: `Textfield`, `ScrollView` + `OverlayScrollBar`, `BubbleBorder`,
`Throbber`, `PrefixSelector`, `WidgetFadeAnimator`.

Data: the launcher index source (desktop entries), `contributes.launcher` rows
from every manifest, the command registry (the palette), `WmModel` windows (the
window picker and the window cycle), and `picker-provider` plugin surfaces,
which answer a query prefix with result rows over JSON-RPC.

ui-tree nodes (a picker provider's result tree): `listItem` → `OmniboxResultView`
over `OmniboxMatchCellView` (`icon`, `label`, `sublabel`, `trailing`); `listItem.highlight`
(S3) → `OmniboxTextView` spans; `listItem.actions` (S4) → `OmniboxSuggestionButtonRowView`;
`section` (S5) → `OmniboxHeaderView`; `keyChips` → the ported key chips as a
trailing node; `textfield` with `icon` and `scope` (S9) → the search box and the
scope chip; `grid` → `TableLayoutView`.

### 2.4 The quick-settings host and its tiles

One host (P29) on an `overlay` surface anchored under the bar's tray button
(or an `xdg_popup` bubble of the bar: `BubbleDialogDelegateView` with
`BubbleBorder::TOP_RIGHT`, `DIALOG_SHADOW`, zero margins, fixed width, arrow-key
traversal, as the extensions menu does). The host owns open and close, focus
and Esc, the page stack, ordering, the "edit tiles" state and the crash badge.
Everything inside is a plugin tree.

| Lifted | Lines / cuts | Role |
|---|---|---|
| `FeatureTile` + an `ActionViewInterface` adapter | 1188 / 6 (ledger A032) | the `tile` face, primary and compact; the five `IsVcDlcUiEnabled` branches are deleted as a block |
| `QuickSettingsSlider` + `UnifiedSliderView` | 658 / 5 (ledger A033, which must add `UnifiedSliderView`) | the `slider` row: icon button, pill slider, read-only variant for the OSD |
| `PageSwitcherView` + `SubpageView` (§1.3) | 293 / 2 | the page stack and the drill-in header (back arrow and title in `BubbleFrameView::SetTitleView`), replacing Ash's `TrayDetailedView` |
| `HoverButton`, `RichHoverButton`, `RichControlsContainerView` (§1.3) | 1568 / 10 | drill-in rows ("Wi-Fi · Home ›"), toggle rows (Bluetooth on/off, Do Not Disturb) |
| `ExtensionsMenuEntryView` | 505 / 5 | the row with a stretching action button, a toggle, a kebab and a pin |
| `ExtensionsMenuMainPageView` and `…SitePermissionsPageView` (copied as page templates) | 803 / 6 and 521 / 5 | the main page shape (header row, master-toggle row, scroll list, footer buttons) and the detail page shape (back header, radio group, toggle row, link) |
| `RichRadioButton` (§1.3) | 152 / 0 | audio output, power profile, display mode pickers |
| `CastDialogSinkView` + `CastDialogSinkButton` + `cast_dialog_helper` | 748 / 6 | device rows with a connecting throbber, a status line and an inline Disconnect button (Bluetooth, outputs, displays) |
| `CastDeviceSelectorView` | 546 / 4 | the output picker under the media card, fed by the generic `DeviceListHost`/`DeviceListClient` mojom that a PipeWire sink lister implements |
| in-menu row widgets from `app_menu.cc` (`InMenuButton`, `InMenuButtonBackground`, `AppMenuView`, the zoom row) | about 500 / 3 | inline `−  100 %  +` rows for volume and brightness steps inside menus and the footer |
| `TabSlider` + `TabSliderButton` (Ash, optional) | 828 / 4 | the one animated segmented control; otherwise `tabSlider` is a row of tonal `MdTextButton`s |
| `FeatureTilesContainerView` (optional) | 467 / 3 | paged tile rows; otherwise a `TableLayoutView` in a `ScrollView` |
| `ShadowFrameView`, `BubbleReopenSuppressor`, `JsonViewBuilder` (§1.3) | | |

Stock fill: `BoxLayoutView` of slider rows, a scrolling `TableLayoutView` grid,
`ToggleButton`, `MdTextButton`, image-button factory buttons, `Label`, `Separator`,
`Throbber`, `Combobox`, `MenuRunner` for the power menu, `ui::DialogModel` for
`action.confirm`, `AnimationBuilder` for open and close.

Data: seven T1 plugins (`examples/quick-settings-*`) bound to the source plugins
through `contributes.quickSettings` entries (`tile`, `slider`, `card`, `footer`,
`page`); every tile is one `ActionItem`, so it is also a palette row.

ui-tree nodes: `tile` → `FeatureTile`; `slider` → `UnifiedSliderView` over
`QuickSettingsSlider`; `listItem` → `HoverButton` (`RichHoverButton` when
`sublabel` or a trailing chevron is set; `RichControlsContainerView` when
`trailing` is a `switch`); `switch` → `ToggleButton`; `checkbox`, `radioGroup` →
stock, or `RichRadioButton` when options carry `icon` and a description;
`select` → `Combobox`; `tabSlider` → `TabSlider` or the `MdTextButton` row;
`grid` → `TableLayoutView`; `scroll` → `ScrollView` + `OverlayScrollBar`
(`RoundedScrollBar` by theme section); `column`/`row` with `container: rounded` →
`BoxLayoutView` + rounded background; `progress` → `ProgressBar` or
`DrawProgressRing`; `badge`, `dot`, `separator`, `spacer`, `label`, `markdown`
(`StyledLabel`), `icon`, `image`, `button`, `iconButton`, `textfield` → stock;
`mediaSession` → §2.9; a `page` entry → `SubpageView` with the host's back
button and the entry's `title`.

### 2.5 Notifications

Two surfaces: toasts (a top-right `overlay` surface, no keyboard) and the
notification centre (a `panel`, in the drawer of §2.8 or a bubble under the
bell). The model is `message_center::MessageCenter`, fed by a new
org.freedesktop.Notifications D-Bus server (views-shell's only new daemon-side
code; `chrome/browser/notifications/notification_platform_bridge_linux.cc` is the
field map, read only, never copied).

| Linked or lifted | Lines / cuts | Role |
|---|---|---|
| `ui/message_center` views (`MessageView`, `NotificationViewBase`, `NotificationView`, header, control buttons, inline reply, large image, `RelativeTimeFormatter`) | link, 5320 / 0 | the card; swipe to dismiss through `SlideOutController` |
| `MessagePopupCollection` + `MessagePopupView` with a views-shell subclass | link, 1816 / 3 (in the subclass) | the toast stack: `ConfigureWidgetInitParamsForContainer` switches the popup widget to the layer-shell type, work-area rects become anchor plus margins, `CanUseTransformForBoundsAnimation` returns true |
| `MessageCenter` model (`MessageCenterImpl`, `NotificationList`, blockers, popup timers) | link, 5365 / 0 | the store; Do Not Disturb is a `NotificationBlocker` |
| Ash `NotificationCenterView` + `NotificationListView` + `MessageViewContainer` + `StackedNotificationBar` + `MessageCenterScrollBar` | 2787 / 7 | the history list with animated insert and collapse, a sticky "Clear all" bar; Linux ships no stock list |
| `OngoingProcessView` (Ash) | 375 / 5 | a pinned live-progress card (recording, transfer, print job) |
| `InfoBarView` + `ConfirmInfoBar` + `InfoBarContainerView` over `components/infobars/core` | 3615 / 6 | the info strip: a banner with icon, message, OK and Cancel, slide-open height animation, stacked by priority; used at the top of quick settings and the launcher and as a thin system-alert strip |
| `ExpandableContainerView` (§1.3) | 163 / 1 | expand a long body |
| `DownloadBubbleRowView` (optional) | 1342 / 12 | a five-column job row with a 3 px progress bar and hover quick actions; `OngoingProcessView` covers most of it with fewer cuts |

Stock fill: `ScrollView`, `WidgetFadeAnimator`, `SlideOutController`, `Label`,
`MdTextButton`, `ImageButton` factory, `Badge` for the unread count on the bell.

Data: the `views-shell.notifications` source plugin publishes the snapshot
(count, do-not-disturb, pinned processes) that bar modules and tiles bind to;
the D-Bus server writes `MessageCenter`; actions invoke commands.

ui-tree nodes: a plugin's `notify` command carries title, body, icon and
actions, drawn by `NotificationView`; a T2 plugin's custom content (S11) is a
`column` subtree rendered into the card's custom content slot (`column`, `label`,
`progress`, `button`, `image`). `banner` (S10) → `InfoBarView`. The centre's
rows are the stock card; `listItem` is not used there.

### 2.6 The OSD

An `overlay` surface, bottom centre, no keyboard, that shows for a moment on a
hardware key: volume, brightness, mic gain, keyboard layout, caps lock, a
"copied" or "workspace 3" pill.

| Lifted | Lines / cuts | Role |
|---|---|---|
| `QuickSettingsSlider` (read-only variant) + `UnifiedSliderView` | shared with §2.4 | the level bar with its icon |
| `FadeLabelView` (from the hover card family) | shared with §2.2 | crossfade between values |
| `FindBarHost` + `FindBarOwner` (as the anchored non-activatable overlay host) | 1077 / 6 | a `TYPE_CONTROL` child widget with `Activatable::kNo`, slide in and out, placed in an owner-supplied box and moved off an `avoid_overlapping_rect`; the file's comment is the rationale for never activating an overlay |
| `StatusBubbleViews` (optional) | 1334 / 4 | the edge-attached pill variant |
| `DownloadProgressRing` | shared with §2.1 | a ring variant for brightness or a countdown |

Patterns: `ZoomBubbleView`'s hover-pauses-autoclose timer;
`DownloadBubblePartialView`'s "auto-show, then dismiss unless touched"
(`WidgetObserver` + `OneShotTimer`).

Stock fill: `WidgetFadeAnimator`, `AnimationBuilder`, `ProgressBar`,
`DrawProgressRing`, `ImageView`, `Label`, `ViewAccessibility::AnnounceText`.

Data: the audio, brightness, power and session sources; caps lock from the
Ozone keyboard state. The OSD draws only what the `views-shell osd show` command
carries (icon, value, label); no plugin draws here. Node mapping for that
command's payload: `icon`, `progress` (`bar` or `ring`), `label`.

### 2.7 The credential modal

Core, never a plugin, and the only password field in the shell (R25). An
`overlay` surface with exclusive keyboard over the `ScrimView`. It is one queue
for the NetworkManager secret agent, the BlueZ agent and the polkit agent.

| Stock or lifted | Lines / cuts | Role |
|---|---|---|
| `ui::DialogModel` + `BubbleDialogModelHost` | stock | the dialog: title, paragraph with icon, `AddPasswordField` (rendered as `EditablePasswordCombobox` with the eye toggle), OK and Cancel; `DialogDelegate::TriggerInputProtection` and `DialogClientView::IsPossiblyUnintendedInteraction` guard the first click |
| `ScrimView` (§1.3) | 67 / 0 | the backdrop (a layer-shell overlay per output, not a child widget) |
| `LoginPasswordView` + `ArrowButtonView` + `LoginButton` + `HoverNotifier` (Ash, optional) | 1409 / 6 | the richer password row: reveal toggle with auto-rehide, caps-lock hint, circular submit arrow with a loading state; `ime_controller` becomes the Ozone caps state |
| `AccessCodeInput` (Ash) | 970 / 3 | PIN and OTP boxes, Bluetooth pairing passkey |
| `AnimatedRoundedImageView` + `RoundedImageView` (Ash) | 523 / 0 | the identity avatar |
| `DownloadBubblePasswordPromptView` | 137 / 3 | the password-with-error block (`kValid`, `kInvalid`, `kInvalidEmpty`) |
| `ExpandableContainerView`, `ExtensionPermissionsView` + `MediaGalleryCheckboxView` | 163 / 1 and 231 / 2 | polkit "details" disclosure and a bulleted list |
| `EmbeddedPermissionPromptBaseView`'s `RequestLineConfiguration` and `ButtonConfiguration` | shape only | the consent prompt as data: icon request lines, three tonal buttons; the permission prompt classes themselves are not lifted |
| `CryptoModulePasswordDialogView` | reference only | the 150-line shape for focus, modality and accessible names; it never clears its password string, which views-shell does |

Data: the three agents (D-Bus, one bus owner per R8). Nothing from a plugin.
No ui-tree node: a tree has no password node, and `action.confirm` uses the
same `DialogModel` path for plain confirmations.

### 2.8 Menus and the drawer

**Menus** are stock: `MenuRunner`, `MenuItemView`, `MenuModelAdapter`,
`ui::SimpleMenuModel`, `MenuButton` + `MenuButtonController`, with the Linux
bubble border and no `USE_ASH_SYS_UI_LAYOUT` (which would turn on blur). The one
shared utility the first prototype re-rolled, the menu-model builder, is written
once over `contributes.menus` locations (`rail/workspace`, `rail/window`,
`bar/<module>`, `tray/<icon>`) and the command registry.

| Lifted | Lines / cuts | Role |
|---|---|---|
| in-menu row widgets from `app_menu.cc` | about 500 / 3 | inline button rows inside a menu (volume `−/+`, a Cut, Copy, Paste style row) |
| `BookmarkMenuController::GetSiblingMenu` and `RunMenuAt` | about 50 lines, pattern | menu-bar sliding: with a menu open, moving over the next bar button opens its menu |
| `MenuExample` (`ui/views/examples/menu_example`) | 715 / 3 | the model, delegate and executor split, and the `RunMenuAt` anchor code |

Node mapping: `common.menu` (S7) names a menu location, so a right-click on any
node opens that location's items.

**The drawer** is Chrome's side panel, lifted for the surfaces that want a
resizable edge pane: the notification centre, the calendar and agenda, a
plugin `panel` surface with a title bar. It is not the quick-settings host (P29).

| Lifted | Lines / cuts | Role |
|---|---|---|
| `SidePanel` | 911 / 8 | the drawer: 8+1 px border, rounded content well, header over the top border, resize grip, open/opening/open/closing states, left or right alignment, per-page width memory (360 default) |
| `ContentParentView` + `ContentParentBackground` | 125 / 3 | the rounded clipping well that rounds any child's layer |
| `SidePanelResizeArea` + `SidePanelResizeHandle` | 233 / 3 | the 4×24 pill grip, keyboard resize in 50 px steps |
| `SidePanelHeader` + `Delegate` | 174 / 1 | the 40 px title row: icon, title, pin, open-in-Chrome, more, close |
| `SidePanelAnimationContentView` | 67 / 0 | the slide-in carrier |
| side panel entry model (`SidePanelEntry`, `SidePanelRegistry`, key, waiter, content proxy) | 1301 / 5 | the page registry: every drawer page registers an entry with a create-content callback; the waiter shows an async page only once populated |
| `SidePanelCoordinator` + `SidePanelUIBase` | 1141 / 8 | toggle, show, close, content swap with caching, focus return |
| `MultiContentsResizeArea` + handle (optional) | 297 / 3 | a splitter between tiles if a surface ever splits |
| `OrganizerPanelView` (alternative) | 1108 / 5 | a flyout shell beside the rail (elevation, focus trap, dismiss on outside click), once its `WebView` body is cut |

The drawer's open and close timings come from `side_panel_animations.cc`
(width 0→1, `ACCEL_80_DECEL_20`, a 150 ms shadow fade) on a `gfx::SlideAnimation`;
the framework behind them is not copied. The shadow and corner where a drawer
meets the main area are `ShadowOverlayView` (345 / 4) and `CustomFloatingCorner`
(from the `CustomCorners` family, 1401 / 4), both optional.

Data: `contributes.surfaces` of kind `panel` register entries; the header's
title and icon come from the manifest. Node mapping: a `panel` surface's tree
renders inside `ContentParentView`; its `title` becomes the `SidePanelHeader`.

### 2.9 Media controls

Linked almost entirely. `components/global_media_controls` and
`components/media_message_center` are content-free, and `services/media_session`
(5225 lines, deps on `base`, `mojo`, `skia`) runs in-process, so Chrome's
producers work unchanged once each MPRIS player is registered as a
`media_session::mojom::MediaSession`.

| Linked or lifted | Lines / cuts | Role |
|---|---|---|
| `MediaProgressView` | link, 892 / 0 | the squiggly seek line |
| `MediaItemUIUpdatedView` + `MediaActionButton` + `MediaLiveStatusView` | link, 1361 / 0 | the desktop card (400 dip): artwork, title, artist, play pill, seek row |
| `MediaItemUIListView` | link, 197 / 0 | the multi-player list |
| `MediaItemManager`, producers, `MediaDialogDelegate`, observers | link, 801 / 0 | the controller behind the bar button and the quick-settings card |
| `MediaSessionItemProducer` + `MediaSessionNotificationItem` | link, 1682 / 1 | the one cut is new code: an MPRIS D-Bus client (template read only: `components/system_media_controls/linux/system_media_controls_linux.cc`) |
| `GetMediaColorTheme` (from `media_item_ui_helper.cc:284-312`) + `notification_theme.h` | 122 / 1 | the 18 roles mapped onto `kColorSys*`, so the pin mixer themes the card |
| `CastDeviceFooterView` | 302 / 0 | "Playing on ‹output› · Stop" footer |
| `CastDeviceSelectorView` (§2.4) | 546 / 4 | the output picker |

Stock fill: a `BubbleDialogDelegateView` implementing `MediaDialogDelegate` as
the bar's media popover; an `ImageButton` in the bar toggled by
`MediaItemManagerObserver::OnItemListChanged`.

Data: the `views-shell.media` source plugin over MPRIS, and the audio source for
sinks. Node mapping: `mediaSession` → `MediaItemUIUpdatedView` (the schema says
`MediaItemUIView`, Ash's clickable variant; both build on Linux, and the desktop
card is the one to name, S8); `mediaSession.outputs` (S8) →
`CastDeviceSelectorView`; a `card` slot in quick settings holds it.

### 2.10 The window cycle and the cheat sheet

Both compose from the sets above. The window cycle (Alt+Tab) is the launcher's
list (`OmniboxResultView` rows over `WmModel` MRU order, the hover card's
thumbnail when screencopy is available) on an `overlay` surface with keyboard
exclusive while held. The cheat sheet is a `TableView` (stock, 4486 lines) of
bindings with the ported key chips (A027, which must also take
`search_result_inline_icon_view` and `keyboard_code_util`, 577 lines, 5 cuts),
sourced from the compositor's bindings list (`GET_BINDINGS` on scroll; the
config file on niri).

### 2.11 Settings pages in Chrome

Not Views. The views-shell Chrome extension renders `contributes.configuration`
as forms in Chrome tabs and reaches the shell through the native messaging host
(architecture §9). Nothing is lifted. Two facts from the colour sweep keep Chrome
and the shell consistent: a `BrowserThemeColor` policy blocks a Web Store theme
(`ThemeService` returns early for theme extensions under a policy theme), so
`theme.chromeThemeId` and the policy writer cannot both be in effect, which
`config.schema.json` must express; and Chrome's own frame colours under the
policy come from `GetAutogeneratedThemeColors` (190 lines, 0 cuts), which the
gallery can lift to predict Chrome's toolbar beside the bar. If an in-shell
settings window were ever wanted (R3 says it is not), `TabbedPane` in
`Orientation::kVertical` (stock, 1459 lines) is the frame, and
`ProfilePickerToolbar` (636 / 3) the multi-step header.

## 3. The node-to-component table

| Node | Component | Surfaces |
|---|---|---|
| `column`, `row` | `BoxLayoutView` (`FlexLayoutView` in the bar); `container: rounded*` adds a rounded background; `elevated` (S12) adds `ShadowFrameView` | all |
| `stack` | `FillLayout` | all |
| `grid` | `TableLayoutView` (`FeatureTilesContainerView` when paged) | quick settings, launcher |
| `scroll` | `ScrollView` + `OverlayScrollBar` or `RoundedScrollBar` | all |
| `repeat` | `TabCollectionAnimatingLayoutManager` over the template; `reorderAction` (S6) adds `DraggedTabsContainer` + `DropArrow` | lists everywhere |
| `label` | `Label`; `highlight` spans (S3) → `OmniboxTextView` | all |
| `markdown` | `StyledLabel` | notifications, quick settings |
| `icon` | `ImageView` from the icon aggregate or the runtime `.icon` loader | all |
| `image` | `ImageView` (`RoundedImageView` when `rounded`) | launcher, media, notifications |
| `badge`, `dot` | `Badge`, `DotIndicator`; `PinnedToolbarButtonStatusIndicator` for `iconButton.indicator` (S13) | bar, rail, launcher |
| `separator` | `Separator`; `ToolbarDivider` in the bar | all |
| `spacer` | flex spacer | all |
| `progress` | `ProgressBar`; `DrawProgressRing`; `DownloadProgressRing` for `state` (S14) | bar, OSD, notifications |
| `button` | `MdTextButton`; `ToolbarChipButton` in the bar | all |
| `iconButton` | image-button factory; `ToolbarButton` in the bar and rail | all |
| `chip` (S2) | `IconLabelBubbleView`; `PermissionChipView` for `variant: indicator` | bar |
| `switch` | `ToggleButton` | quick settings |
| `checkbox`, `radioGroup` | `Checkbox`, `RadioButton`; `RichRadioButton` with icon and description | quick settings, dialogs |
| `select` | `Combobox` | quick settings |
| `slider` | `UnifiedSliderView` over `QuickSettingsSlider` | quick settings, OSD |
| `textfield` | `Textfield`; with `icon` and `scope` (S9) the search box and `SelectedKeywordView` | launcher |
| `listItem` | `HoverButton` / `RichHoverButton` / `RichControlsContainerView`; `OmniboxResultView` in the launcher; `CastDialogSinkView` for `variant: device` (S15) | quick settings, launcher, drawer |
| `section` (S5) | `OmniboxHeaderView` | launcher, quick settings |
| `tile` | `FeatureTile` | quick settings |
| `tabSlider` | `TabSlider` or the tonal `MdTextButton` row | quick settings |
| `keyChips` | `KeyboardShortcutView` | cheat sheet, launcher |
| `mediaSession` | `MediaItemUIUpdatedView` (+ `CastDeviceSelectorView`) | quick settings, bar popover |
| `banner` (S10) | `InfoBarView` | quick settings, launcher, alerts |
| `expandable` (S16) | `ExpandableContainerView` | notifications, dialogs |
| `avatar` (S17) | `BadgedProfilePhoto` / `RoundedImageView` | bar, user menu, credential modal |

## 4. Schema additions this implies

Each addition names the component that makes it drawable. None adds a colour
literal, a pixel size or a password node.

| # | Addition | Component behind it |
|---|---|---|
| S1 | `contributes.bar` (open decision P28): entries `{id, slot: start/centre/end, ui, state, order, popover: <surface id>, when}`, the bar's analogue of `quickSettings` | the bar's `FlexLayout` groups and `ToolbarController` |
| S2 | `chip` node: `icon`, `label`, `variant: default/indicator/request`, `role`, `flash` (show the label, then collapse), `action` | `IconLabelBubbleView`, `PermissionChipView`, `OmniboxChipButton` |
| S3 | `label.highlight` and `listItem.highlight`: a binding to `[[offset, length]]` ranges (what a picker provider or the ranker returns) | `OmniboxTextView` |
| S4 | `listItem.actions`: an array of `{label, icon, action}` pills shown under or beside the row | `OmniboxSuggestionButtonRowView` |
| S5 | `section` node: `title`, `children`, lazily headed when the group changes | `OmniboxHeaderView`, `OmniboxRowView` |
| S6 | `repeat.reorderAction`: fires `{key, index}` after a drag reorder; `repeat.animate` default true | `DraggedTabsContainer`, `TabDragScrollHandler`, `DropArrow` |
| S7 | `common.menu`: a `contributes.menus` location opened on right-click or long-press | the menu-model builder over `MenuRunner` |
| S8 | `mediaSession`: name `MediaItemUIUpdatedView`; add `outputs` (a binding to the sink list) and `outputAction` | `CastDeviceSelectorView` over the `DeviceListHost` mojom |
| S9 | `textfield.icon` (leading) and `textfield.scope` (`{label, shortLabel, icon}` or a binding) | `SearchBoxViewBase`, `SelectedKeywordView` |
| S10 | `banner` node: `icon`, `text`, `actions` (OK, Cancel), `dismissible`, `priority` | `InfoBarView`, `InfoBarContainerView` |
| S11 | `contributes.notifications` custom content: a `column` subtree a T2 plugin attaches to its own notification | `NotificationView`'s custom content slot |
| S12 | `column/row.container: elevated` | `ShadowFrameView` |
| S13 | `iconButton.indicator: none/engaged` | `PinnedToolbarButtonStatusIndicator` |
| S14 | `progress.state: idle/dormant/busy/determinate` for `shape: ring` | `DownloadProgressRing` |
| S15 | `listItem.variant: device` with `status: idle/connecting/connected/error` and a `disconnectAction` | `CastDialogSinkView` |
| S16 | `expandable` node: `title`, `children` | `ExpandableContainerView` |
| S17 | `avatar` node: `src`, `badge` (icon), `ring: none/dotted/gradient` | `BadgedProfilePhoto`, `RoundedImageView` |
| S18 | `surfaces[].kind: panel` gains `edge: left/right`, `width` (DIP, within the drawer's 360 default and bounds) and `resizable` | `SidePanel`, `SidePanelResizeArea` |

Beyond the tree, three inventory lines must change so the composition holds:
`views::HighlightBorder` is ChromeOS-only in stock (`ui/views/BUILD.gn` adds it
under `is_chromeos`), so INVENTORY §1 and ledger A007 name a stock twin that does
not exist on Linux (use `BubbleBorder` or a themed rounded-rect border, or add a
252-line, 2-cut row); A027 must list `search_result_inline_icon_view` and
`keyboard_code_util`; A033 must list `UnifiedSliderView`; and the "107
`kColorSys*` ids" count is 113 names at this tag.

## 5. The first three surfaces to build, in order

Foundations first (step 0), then the three surfaces where the lifted components
compose. Architecture §8 orders the OSD and the notification toasts earliest
because they have the fewest dependencies; both stay where they are, because the
OSD is the `QuickSettingsSlider` port plus stock fades, and the toasts are a
link with one subclass, so neither waits on this order and neither needs a
composition decision.

### Step 0: foundations

The lifts of §1, in dependency order: the views-shell `LayoutProvider` extension
enum, `views_shell_color_id.h` and the pin mixer with the two lifted colour
recipes (362 + 165 + 179 lines, 12 cuts), the icon aggregate target with the
`chrome/app` and omnibox subsets and the runtime `.icon` loader (589 / 2), the
`ActionId` range (640 / 3 as pattern), `BrowserLayoutParams` (249 / 0), `ScrimView`
(67 / 0), `ShadowFrameView` (168 / 1), `BubbleReopenSuppressor` (188 / 0),
`PageSwitcherView` + `SubpageView` (293 / 2), `HoverButton` + `RichHoverButton` +
`RichControlsContainerView` (1568 / 10), `RichRadioButton` + `ExpandableContainerView`
(315 / 1), `ToolbarButton` + ink drop util (1444 / 5),
`TabCollectionAnimatingLayoutManager` (1074 / 0), `RoundedScrollBar` (242 / 0),
`OmniboxMouseEnterExitHandler` (81 / 0). The ui-tree renderer's engine,
`JsonViewBuilder` (3461 / 6), belongs here too, since step 3 is its first
consumer. About 10,900 lines, 40 cuts, most of them the two shims.

### Step 1: the bar

Why first: SPEC C4.2 and C4.3 already draw the 32 px bar and prove the
`xdg_popup` path, and every later surface anchors a popover to it. Exact set:

`ToolbarButton` (step 0), `ToolbarChipButton` (150 / 1), `ToolbarDivider` (43 / 1),
`ToolbarIconContainerView` (447 / 2), `PinnedToolbarActionsContainerLayout` (217 / 0),
`PinnedToolbarButtonStatusIndicator` (162 / 0), `ToolbarController` + `OverflowMenu`
+ `OverflowButton` (1707 / 6), `AppMenuButton` + `AppMenuControl` (365 / 4),
`IconLabelBubbleView` + strategies + `location_bar_util` (1989 / 3),
`OmniboxChipButton` (336 / 2), `PermissionChipView` + `MultiImageContainer` (1011 / 6),
`PermissionDashboardView` (324 / 2), `LocationBarLayout` (249 / 0),
`DownloadProgressRing` (126 / 2), `BadgedProfilePhoto` (203 / 2), the
`ToolbarView::InitLayout` recipe and the `OnMouseWheel` pattern. 8,625 lines,
34 cuts. Stock: `Label`, `MenuButton`, `MenuRunner`, `FlexLayoutView`,
`AnimatingLayoutManager`, `Badge`, `DotIndicator`, `BubbleDialogDelegateView`,
`BubbleSlideAnimator`. Data: `WmModel` and the audio, network, bluetooth, power,
session and notifications sources. Schema: S1, S2, S13, S14. The optional pieces
(`StatusBubbleViews`, the pinned actions container, the horizontal strip) wait
for step 2.

### Step 2: the workspace rail on the vertical tab strip

Why second: it is the hero surface, the largest lift, and it needs step 1's
buttons, chips and popover path plus the `WmModel` that SPEC C5 delivers. It is
built in two slices.

Slice A, the static rail (a mirror that clicks): shared types (279 / 1),
`TabCollectionAnimatingLayoutManager` (step 0), `TabView` + vertical layout +
`TabStyleViews` + `VerticalTabStyleViews` + `TabStyle` (2362 / 8), `TabIcon`
(980 / 6), `TabCloseButton` + `GlowHoverController` + `TabTitle` (512 / 2),
`PinnedTabContainerView` + `UnpinnedTabContainerView` + layouts (1802 / 4),
`TabStripView` + layout (1349 / 5), `TabCollectionNode` + root + controller
retyped onto `WorkspaceNode` (2056 / 4), `VerticalTabStripStateController` (792 / 7),
`VerticalTabStripRegionView` + locks + animations (1683 / 12),
`VerticalTabStripTopContainer` + `TopContainerButton` (763 / 7),
`VerticalTabStripBottomContainer` (168 / 3), `RoundedScrollBar` +
`VerticalTabStripScrollBar` (330 / 1), `TabStripFlatEdgeButton` (466 / 3), the
tab strip colour mixers (362 / 5, in the pin mixer) and `tab_group_color_ids.h`
(78 / 0). 15,056 lines, 68 cuts as counted per component; the `WorkspaceNode`
retyping and the two shims recur across them. Stock: `AccessiblePaneView`,
`ResizeArea`, `FlexLayout`, `Separator`, `ScrollView`, `MenuRunner` for the
`rail/*` menu locations. Data: the `WmModel`; commands `focus-workspace`,
`focus-window`, `toggle-collapsed`, `toggle`. The acceptance target is the one
`shell/PROVENANCE.md` names: a rail click that switches a workspace and is
observed through the compositor's echo.

Slice B, the full rail: `TabGroupView` family (2541 / 6) for workspaces as
groups, `SplitTabView` (484 / 3), `AlertIndicatorButton` (709 / 5),
`DraggedTabsContainer` + `TabDragScrollHandler` + `TabDragHandler` rewrite +
`DropArrow` (2942 / 10), the hover card family (3714 / 6), the editor bubble
rewritten over `ColorPickerView` (part of 2181 / 8), the rail frame chrome
(956 / 3), `VerticalTabStripFocusSwipeController` (219 / 1) and
`TabStripComboButton` (669 / 6). 11,346 lines and 33 cuts for the first six.
Schema: S6, S7.

### Step 3: the quick-settings host and its tiles

Why third: it is the first consumer of the ui-tree renderer, so every T1 plugin
in `examples/quick-settings-*` lights up at once, and it reuses the rows and
page stack of step 0 and the popover path of step 1. Exact set:

`FeatureTile` + `ActionViewInterface` adapter (1188 / 6, A032),
`QuickSettingsSlider` + `UnifiedSliderView` (658 / 5, A033), `PageSwitcherView` +
`SubpageView`, `HoverButton` + `RichHoverButton` + `RichControlsContainerView`,
`RichRadioButton`, `JsonViewBuilder` (all step 0), `ExtensionsMenuEntryView`
(505 / 5), `CastDialogSinkView` + button + helper (748 / 6), the in-menu row
widgets (about 500 / 3), `ShadowFrameView`, `BubbleReopenSuppressor`. 8,573 lines
of new lifts beyond step 0, 40 cuts. Stock: `BubbleDialogDelegateView`,
`BoxLayoutView`, `TableLayoutView`, `ScrollView` + `OverlayScrollBar`,
`ToggleButton`, `MdTextButton`, the image-button factory, `Label`, `Separator`,
`Throbber`, `Combobox`, `MenuRunner`, `ui::DialogModel`, `AnimationBuilder`.
Data: the seven T1 plugins over the source plugins through
`contributes.quickSettings`. Schema: S5, S8, S10, S12, S15, S16. The optional
`TabSlider` (828 / 4), `FeatureTilesContainerView` (467 / 3) and
`CastDeviceSelectorView` (546 / 4, with the media card) follow once the host
draws.

After these three, the launcher (§2.3, 5,500 lines of omnibox rows, 36 cuts
plus the ranker), the notification centre list and info strip (§2.5), the drawer
(§2.8) and the media stack (§2.9, a link plus the MPRIS client) compose from
the same foundations with no further shim.
