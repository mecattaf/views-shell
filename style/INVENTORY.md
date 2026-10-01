# views-shell/style: inventory

`views-shell/style` is views-shell's public component kit. It is **stock Chromium Views**: the
`ui/views` controls under one include root, the `ui/color` system roles fed by one
theme mixer, a views-shell `LayoutProvider` and `TypographyProvider`, and five small
ports from Ash where stock Views has no twin. Every views-shell surface and the UI-tree
renderer are assembled from it. Only T0 (built-in) code links it directly. T1 and
T2 plugins reach it through the `ui` tree.

Views first: the views views-shell wants are the least wm-coupled code in Ash, and the
Ash code that is least welded to the window manager is, for that same reason,
mostly a thin subclass of a stock control (`ash::Switch` is an 83-line
`views::ToggleButton`; `ash::PillButton` is a `views::LabelButton`). Where stock
Views ships the twin, the twin wins. The port policy is in
[`../ASH-PORT-LEDGER.md`](../ASH-PORT-LEDGER.md).

Upstream revision for this inventory: Chromium **154.0.8037.92** (Linux Stable,
commit `334b65d254ccc35df4fca82706d1753227b01039`). Directory listings were read
from the public source. The rebalance that produced this version is
the private scoping note `views-first-rebalance.md`.

Status key: **link** = builds on Linux, linked from `//ui` or `//components` as is
and re-exported; **port** = copy the BSD-3 file into `views-shell/style`, keep the header,
replace Ash seams (ledgered); **new** = written for views-shell; **out** = not taken.

## 1. Stock `ui/views` controls (the kit)

Linked as is. `views-shell/style/views.h` re-exports them so that T0 code has one include
root. No component code changes for colour: they pick up the theme through the
mixer (section 2).

| Control | Upstream | Replaces the Ash variant |
|---|---|---|
| Label, StyledLabel, Link | `ui/views/controls/label.h`, `styled_label.h`, `link.h` | `bubble_utils`, `RoundedLabel` |
| MdTextButton (`kDefault`, `kTonal`, `kProminent`, `kText`) | `ui/views/controls/button/md_text_button.h` | `PillButton` |
| ImageButton, ToggleImageButton (from `CreateVectorImageButtonWithNativeTheme`, `CreateVectorToggleImageButton`) | `ui/views/controls/button/image_button*.h` | `IconButton`, `CloseButton` |
| LabelButton, MenuButton | `ui/views/controls/button/` | |
| ToggleButton | `ui/views/controls/button/toggle_button.h` | `ash::Switch` |
| Checkbox, RadioButton | `ui/views/controls/button/` | `Checkbox`, `RadioButton`, `OptionButton*` and their groups |
| Textfield, Textarea, PrefixSelector | `ui/views/controls/textfield/`, `textarea/`, `prefix_selector.h` | `SystemTextfield` |
| Combobox, EditableCombobox | `ui/views/controls/combobox/`, `editable_combobox/` | `ash::Combobox` (which reads `ash/wm/work_area_insets.h`) |
| Slider, ProgressBar, Throbber, `progress_ring_utils` | `ui/views/controls/` | |
| Badge, DotIndicator | `ui/views/controls/badge.h`, `dot_indicator.h` | `ash::DotIndicator` |
| ImageView, Separator | `ui/views/controls/` | |
| ScrollView, OverlayScrollBar | `ui/views/controls/scroll_view.h`, `scrollbar/` | `RoundedScrollBar` |
| TableView, TreeView, TabbedPane | `ui/views/controls/table/`, `tree/`, `tabbed_pane/` | |
| MenuRunner and the menu stack, `ui::SimpleMenuModel` | `ui/views/controls/menu/`, `ui/menus/` | the power button menu |
| FocusRing, HighlightPathGenerator, InkDrop | `ui/views/controls/focus_ring.h`, `highlight_path_generator.h`, `ui/views/animation/ink_drop.h` | `style_util` |
| BubbleDialogDelegateView, BubbleBorder, HighlightBorder, Widget | `ui/views/bubble/`, `ui/views/widget/` | `bubble_utils`, `SystemShadow` |
| BoxLayoutView, FlexLayoutView, TableLayoutView, AnimatingLayoutManager, BoundsAnimator, AnimationBuilder | `ui/views/layout/`, `ui/views/animation/` | `RoundedContainer` layouts, `TrayDetailedView` transitions |
| `views::WidgetFadeAnimator` | `ui/views/animation/widget_fade_animator.h` | OSD and toast fades |
| `ui::ActionItem` + `views::ActionViewController` | `ui/actions/`, `ui/views/actions/` | the command registry and every tile (one action item each) |
| `ui::DialogModel` + `BubbleDialogModelHost` | `ui/base/models/dialog_model.h`, `ui/views/bubble/bubble_dialog_model_host.h` | `SystemDialogDelegateView`, the credential modal's password field |
| `message_center` views (`NotificationView`, `MessagePopupView`) | `ui/message_center/views/` | `AshNotificationView`, `CounterExpandButton` |
| `MediaItemUIView` | `components/global_media_controls/public/views/` | Ash's media view |

`views::WebView` and every WebUI bubble are out: views-shell links no `//content`
(rule R24). Web pages live in Chrome.

## 2. Colour, type and shape (the theming layer)

| Component | Upstream | Status | Notes |
|---|---|---|---|
| `ui::ColorProvider`, mixers, `ColorProviderKey`, the 107 `kColorSys*` ids, `sys_color_mixer`, `ref_color_mixer`, `dynamic_color/` | `ui/color/` | link | The Material system roles on every platform. No `cros.sys` layer and no Ash colour ids. |
| **Theme pin mixer** (Omarchy `colors.toml` → `kColorSys*`) | none | new | One `ColorMixer` appended last with `ColorProviderManager::AppendColorProviderInitializer`. The binding is [`theme-map.json`](theme-map.json). |
| **Omarchy `colors.toml` resolver** | `basecamp/omarchy` `bin/omarchy-theme-color` (MIT) at `c05d9019` | port | The cascade that fills derived keys (`color0` = background, `bright_*` = +20% white, and so on). Ported with its MIT notice; byte fixtures in, mixer rows out. |
| Seed (`ColorProviderKey.user_color`, `kAccent` source, tonal-spot) | `ui/color/`, `ThemeService::GetColorProviderKey` pattern | new (pattern) | The same neutralised seed Chrome receives through the `BrowserThemeColor` policy, so the shell and the browser are one tonal family. |
| `views::LayoutProvider`, `TypographyProvider`, `PlatformStyle` | `ui/views/layout/`, `ui/views/style/` | link, subclassed | As Chrome's `ChromeLayoutProvider` and `ChromeTypographyProvider` do. `shell.toml [font] base-size` and `[spacing] scale` scale them. Fonts come from the user's configuration. |
| Vector icons | `ui/views/vector_icons/`, `components/vector_icons/` (link); `ash/resources/vector_icons/` subset (port, A040) | link + port | Only the Ash icons a first-party plugin names. `xdg:` names for freedesktop icons. |
| Dark/light mode controller, colour palette controller | `ash/style/dark_light_mode_controller_impl.*`, `color_palette_controller.*` | out | welded to `ash::Shell`; the theme is passed in |
| Blurred background shield | `ash/style/blurred_background_shield.*` | out | no blur by choice; panels take the theme's background |

## 3. The five ports

Each is ledgered in [`../ASH-PORT-LEDGER.md`](../ASH-PORT-LEDGER.md).

| Component | Upstream | Row | Why it is ported |
|---|---|---|---|
| **FeatureTile** (primary, compact; icon, label, sublabel, drill-in, toggled) | `ash/system/unified/feature_tile.{h,cc}` | A032 | The quick-settings tile face. No Shell reference, no stock twin. Bound to a `ui::ActionItem` through a small `ActionViewInterface` adapter. |
| **QuickSettingsSlider** | `ash/system/unified/quick_settings_slider.*` | A033 | A `views::Slider` subclass with no Shell reference; the slider slot and the OSD share it. |
| **KeyboardShortcutView** (key chips), TextImage | `ash/style/keyboard_shortcut_view.*`, `text_image.*` | A027 | No stock key chip. Ported when the cheat sheet starts. |
| Vector icon subset | `ash/resources/vector_icons/*.icon` | A040 | Data, not code. |
| `string_matching` (fuzzy tokenized ranker) | `chromeos/ash/components/string_matching/` | A041 | Depends only on `base/`; the launcher's ranking. |

Everything else that earlier drafts planned to port from `ash/style`,
`ash/system/tray`, `ash/controls`, `ash/bubble` and `ash/system/toast` is
dropped, with its stock replacement named in the ledger.

## 4. New paints (not in the core)

A segmented control is a row of `MdTextButton`s (tonal when selected), and the
countdown ring is `views::DrawProgressRing` alone, so neither is a new paint. Four
paints are genuinely new and none is in the core: a range slider, an HSV colour
picker, a line graph and a screen corner.

## 5. The gallery

The kit ships a gallery, designed in [`gallery/DESIGN.md`](gallery/DESIGN.md) on
the `ui/views/examples` framework (`ExampleBase`, `CreateExamples`). It is both the
showcase and the conformance harness. Upstream's new Views Canvas and
`JsonViewBuilder` in `ui/views/examples` (2026-07-30 onward) build a live View
hierarchy from a JSON tree, which is the same declarative pattern the views-shell `ui`
tree uses; open decision P9 tracks whether views-shell follows, copies or links it.
