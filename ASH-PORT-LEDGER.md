# Ash port ledger

Every file views-shell copies from `//ash`, `//ui/chromeos` or another Chromium directory
that views-shell does not link is recorded here (rule R5). Files linked as is from
`//ui/views`, `//ui/color`, `//ui/message_center` and the other Linux-buildable
directories are **not** ledgered: they come with the Chromium tree.

## Format

| Column | Meaning |
|---|---|
| # | stable row number; never reused |
| Upstream path | path in `chromium/src` |
| Upstream revision | the Chromium tag and commit the file was copied from. `planned @ <tag>` means the row is planned and was surveyed at that tag, but nothing is copied yet. |
| Header kept | `yes` once the copied file keeps its original "Copyright … The Chromium Authors" BSD-3 header unchanged. Required. A views-shell provenance line is added below it: `// Ported to views-shell from <path> @ <tag>.` |
| views-shell path | where the file lives in this repository |
| Local changes | every deviation from upstream, in words. "none" is a claim, checked at each hop. |
| Status | `planned`, `ported`, `retired` (upstream change made it unnecessary), `dropped` (with reason) |

At each hop, step 6 of [`docs/chromium-hop.md`](docs/chromium-hop.md) diffs every
`ported` row against the new upstream revision, records each hunk pulled or
skipped in that hop's ledger, and moves the row's revision.

Survey tag for the planned rows: **154.0.8037.92** (Linux Stable, commit
`334b65d254ccc35df4fca82706d1753227b01039`). Paths were checked against that tag.

**Views first.** Stock `ui/views` is the donor and Ash is the accent: the views
views-shell wants are the least wm-coupled code in Ash, and for the same reason most of
them are thin skins over a stock Views control that views-shell can use directly. So the
ledger is short on purpose. Surfaces are built from stock Views. A surface adds a
row only under the policy below. The 2026-10-01 rebalance moved 35 of the 40 rows
first planned here to Dropped, each with its stock replacement named
(the private scoping note `views-first-rebalance.md`).

## Policy

Views first. A file is ported only when (1) it has no `ash::Shell`, root-window,
session or window-manager reference, (2) stock `ui/views`, `ui/color`, `ui/base`
or `components/` has no equivalent within a subclass, and (3) a core or
first-party plugin surface needs it now. Everything else is pattern, recorded
here as dropped with its stock replacement.

## Rows

| # | Upstream path | Upstream revision | Header kept | views-shell path | Local changes (planned) | Status |
|---|---|---|---|---|---|---|
| A027 | `ash/style/keyboard_shortcut_view.{h,cc}, text_image.{h,cc}` | planned @ 154.0.8037.92 | (on copy) | `style/` | namespace ash -> views-shell; colour ids -> kColorSys* | planned (with the cheat sheet) |
| A032 | `ash/system/unified/feature_tile.{h,cc}` | planned @ 154.0.8037.92 | (on copy) | `style/feature_tile.{h,cc}` | drop the VC DLC download states and `ash_features`; delete the unused dark_light_mode_controller_impl include; colour ids -> kColorSys*; an ActionViewInterface adapter beside it | planned |
| A033 | `ash/system/unified/quick_settings_slider.{h,cc}` | planned @ 154.0.8037.92 | (on copy) | `style/quick_settings_slider.{h,cc}` | colour ids -> kColorSys*; ash_strings -> views-shell strings | planned |
| A040 | `ash/resources/vector_icons/*.icon` (subset) | planned @ 154.0.8037.92 | n/a (data) | `resources/vector_icons/` | only the icons a first-party plugin names; a check fails on an unused or missing one | planned |
| A041 | `chromeos/ash/components/string_matching/*` | planned @ 154.0.8037.92 | (on copy) | `launcher/string_matching/` | BUILD.gn without `assert(is_chromeos)`; namespace ash -> views-shell | planned |

## Dropped

| Row or path | Stock replacement or reason |
|---|---|
| A001–A003 colour ids, mixer, provider source | `ui/color` kColorSys* and one views-shell mixer fed by the Omarchy theme input (`style/theme-map.json`) |
| A004 color_util | `ui/color` transforms |
| A005 style_util | `views::InkDrop`, `views::FocusRing`, `InstallRoundRectHighlightPathGenerator` |
| A006 typography | views-shell `views::TypographyProvider` over `views::style` |
| A007 system_shadow | `views::BubbleBorder`, `views::HighlightBorder` |
| A008–A011 cros json5 tokens | not needed: kColorSys* replaces cros.sys |
| A012 pill_button | `views::MdTextButton` |
| A013 icon_button | `views::ImageButton` from the image-button factory |
| A014 switch | `views::ToggleButton` |
| A015–A017 option, checkbox, radio | `views::Checkbox`, `views::RadioButton` |
| A018 combobox | `views::Combobox` (Ash's reads `ash/wm/work_area_insets.h`) |
| A019 tab_slider | `MdTextButton` row |
| A020 pagination_view | quick settings scroll |
| A021 system_textfield | `views::Textfield` |
| A022–A023 rounded container, rounded label | rounded background, `views::Label` |
| A024 dot_indicator | `views::DotIndicator` |
| A025 counter_expand_button | `ui/message_center` views |
| A026 close_button | image-button factory |
| A028 error_message_toast | `views::Label` |
| A029 system_dialog_delegate_view | `ui::DialogModel` + `BubbleDialogModelHost` |
| A030 rounded_rect_cutout_path_builder | no user |
| A031 style_viewer | `ui/views/examples` framework |
| A034 hover_highlight_view | stock row composition |
| A035 tray_detailed_view | the quick-settings host's page frame |
| A036 tray kit | stock `views::Button` pills |
| A037 rounded_scroll_bar | `views::OverlayScrollBar` |
| A038 bubble_utils | `views::Label` + `views::style` |
| A039 system_toast_view | stock composition; the toast API is kept |
| `ash/style/dark_light_mode_controller_impl.*` | welded to `ash::Shell`; the theme is passed in |
| `ash/style/color_palette_controller.*` | wallpaper-derived colour is out |
| `ash/style/blurred_background_shield.*` | no blur by choice |
| everything under `ash/webui` | system web apps are out; views-shell pages live in Chrome |
