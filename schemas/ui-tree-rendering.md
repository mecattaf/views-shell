# UI-tree rendering and the plugin registry view: the reference contracts

Two contracts the C++ must reproduce byte for byte, each written first as a
Python reference with committed fixtures, so the C++ is tested against something
that existed before it:

| Contract | Reference | Fixtures | C++ owner |
|---|---|---|---|
| render trace of a `ui` tree | [`../tools/ui-tree-render.py`](../tools/ui-tree-render.py) | `tools/fixtures/render/<example dir>.<ui file>.json`, from `tools/fixtures/snapshots/<example dir>.json` | the ui-tree renderer (`shell/ui_tree/`) |
| registry view of a manifest | [`../tools/plugin-registry.py`](../tools/plugin-registry.py) (its docstring is the field-by-field contract) | `tools/fixtures/registry/<plugin id>.json` | the plugin host (`shell/plugins/`) |

`tools/validate.sh` runs both in `--check-all` mode and prints `registry ok (N)`
and `render ok (N)`. After a deliberate change to an example, a schema or this
contract, regenerate with `--write-all` and review the fixture diff in the same
commit. Both tools validate their input with `tools/validate.py`'s own
validators first: an invalid manifest or tree prints `REJECT <file> <reason>`
and is never registered or rendered.

Both outputs are JSON with sorted keys, two-space indent, non-ASCII kept as is,
and one final newline (Python `json.dumps(obj, indent=2, sort_keys=True,
ensure_ascii=False) + "\n"`). Arrays keep their source order unless a rule says
sorted.

## The render trace

```json
{
  "plugin": "example.power-menu",
  "root": {
    "node": "column",
    "view": "views::BoxLayoutView",
    "layout": "box-vertical",
    "props": { "container": "rounded", "spacing": "normal" },
    "children": [ ... ],
    "error": null
  }
}
```

`plugin` is the plugin id used to qualify bare command names (from the manifest
two directories above the ui file, or `--plugin`), else `null`. Every trace node
has exactly the six keys `node`, `view`, `layout`, `props`, `children`, `error`.

### The view table

`view` is the stock Views class a node becomes. This table is the contract; a
node type outside it is rejected by the schema before rendering.

| node | view | layout |
|---|---|---|
| `column` | `views::BoxLayoutView` | `box-vertical` |
| `row` | `views::BoxLayoutView` | `box-horizontal` |
| `stack` | `views::View` | `fill` (`views::FillLayout`) |
| `grid` | `views::TableLayoutView` | `table` |
| `scroll` | `views::ScrollView` | null |
| `repeat` | null: it expands (see below) | null |
| `label` | `views::Label` | null |
| `icon` | `views::ImageView` | null |
| `image` | `views::ImageView` | null |
| `badge` | `views::Badge` | null |
| `dot` | `views::DotIndicator` | null |
| `separator` | `views::Separator` | null |
| `spacer` | `views::View` | null |
| `progress` | `views::ProgressBar` | null |
| `button` | `views::MdTextButton` | null |
| `iconButton` | `views::ImageButton` | null |
| `switch` | `views::ToggleButton` | null |
| `checkbox` | `views::Checkbox` | null |
| `radioGroup` | `views::BoxLayoutView` of `views::RadioButton` | `box-vertical` |
| `select` | `views::Combobox` | null |
| `slider` | `views::Slider` | null |
| `textfield` | `views::Textfield` | null |
| `listItem` | `views_shell::ListItemView` (a `views::Button` of icon, label and sublabel, with the trailing node) | null |
| `markdown`, `tile`, `tabSlider`, `keyChips`, `mediaSession` | null, with `error: "not drawn in chapter 2"` | null |

The undrawn nodes still carry their resolved `props`, so the trace already says
what the face will receive. Why each one waits is in [Not drawn in chapter
2](#not-drawn-in-chapter-2); what else a drawn node may report is in [Render
errors](#render-errors).

The C++ renderer (`shell/ui_tree/`, w3a) writes `view` from the view it actually
built: `views::` plus the class's metadata name (`View::GetClassName()`), or
`views_shell::ListItemView`. A trace that matches the fixture therefore also says
that every node became the class this table names.

### props

`props` holds the node's own properties as written, minus `type` and the
node-valued keys, with every value resolved:

- A binding `{"$bind": ...}` is replaced by its value (next section). Literals,
  semantic roles (`role`), typography tokens (`typography`), spacing, sizes and
  variants stay the enum strings they are; the trace never contains a colour
  literal (rules R11, R17). Schema defaults are not filled in, so an absent
  property stays absent.
- `action` and `iconAction` become `{command, args, confirm, close}`: `command`
  resolved and qualified (a bare `toggle` becomes `<plugin id>/toggle`), `args`
  resolved recursively (default `{}`), `confirm` resolved text or null, `close`
  default false.
- `radioGroup` and `select` `options` are resolved to the option array.

### children

Node-valued properties become `children`, in this order: `children` (each
entry), `child` (`scroll`), `template` (`repeat`), `trailing` (`listItem`).
`radioGroup` gets one child per option, `{node: "option", view:
"views::RadioButton", layout: null, props: {value, label, icon?, checked},
children: [], error: null}`, where `checked` is `option.value == props.value`.

### repeat

`repeat` expands its `template` once per item of the bound array, in order, as
its `children`. Its props keep `items` as the binding (`{"$bind": "/actions"}`),
`key` and `direction` as written, plus `count` (the number of items) and, when
`key` is set, `keys` (each item's resolved key, for add/remove/move animation).

- In a list of children the repeat has `view: null`: the renderer inserts the
  copies into the parent in place of the repeat.
- In a one-child slot (the root, `scroll.child`, `listItem.trailing`) the copies
  need a container: the repeat becomes `views::BoxLayoutView` with layout
  `box-vertical` (`direction: column`, the default) or `box-horizontal` (`row`).
- When `items` does not resolve to an array, `count` is 0, there are no
  children, and `error` names the pointer.
- Without a snapshot, the repeat has exactly one child: the template, its
  bindings left as written.

### Bindings

A snapshot file is `{"snapshot": <the entry's state or the T2 plugin's snapshot>,
"sources": {"<plugin>/<source>": <that source's snapshot>}}`.

- `$bind` is a JSON Pointer (RFC 6901: `~1` is `/`, `~0` is `~`, an array index
  is decimal without leading zeros). An absolute pointer reads the snapshot.
  Inside a repeat, `.` is the current item and `./a/b` reads `/a/b` of it.
  With `source`, the pointer reads that source's snapshot.
- A value that does not resolve becomes `{"$unresolved": "<the $bind string>"}`;
  `null` is a resolved value. The committed fixtures contain no `$unresolved`
  (`--check-all` fails on one).
- Without `--snapshot`, every binding stays as written, `{"$bind": ...}` with
  its `format` and `source`, and a bound `icon` or `src` is not checked (Render
  errors).
- The C++ reads JSON with `base::JSONReader`, which turns an integer outside the
  32-bit range into a double. An unformatted value of that kind is written as a
  double (`4294967296.0`) where the Python keeps the integer; `text` and the
  other formats print both the same. No fixture holds one.

### Formats

`format` applies to the resolved value. A value of the wrong kind for its format
is rendered as with `text`. Rounding is `std::round` (halves away from zero).

| format | input | output |
|---|---|---|
| none | any | the JSON value unchanged |
| `text` | any | string as is; `true` / `false`; an integer, or a double with no fraction, in decimal without `.0`; another double in its shortest round-trip form; `null` as `""`; arrays and objects as compact JSON with sorted keys |
| `percent` | a number, 1 = 100 % | `round(v × 100)` then `%`: `0.855` → `86%` |
| `duration` | seconds | floor to whole seconds, then `D d H h` (at least a day), `H h M min` (at least an hour), `M min`, or `S s`: `93784` → `1 d 2 h` |
| `bytes` | a byte count | below 1024 `N B`; else divided by 1024 into KB, MB, GB, TB, PB (stop below 1024, PB at most), one decimal below 100 (`round(v × 10) / 10`, so `1.5 KB`), whole above (`512 MB`) |
| `time` | epoch seconds, or an ISO 8601 instant `YYYY-MM-DDTHH:MM[:SS[.f…]]` followed by `Z`, `±HH:MM` or `±HHMM` (nothing else is an instant) | `HH:MM`, 24-hour, in UTC |
| `relative-time` | as `time` | against the fixed clock `2026-10-02T00:00:00Z`: under 60 s `now`; else `N min`, `N h` or `N d` (floored) followed by `ago`, or preceded by `in` for a future instant |

UTC and the fixed clock keep the trace deterministic. The drawn shell formats
`time` and `relative-time` in the local zone against the real clock; the trace
is the shape of that call, not its wording. In the C++ both are parameters of
the render (`RenderOptions::now` and `RenderOptions::zone`), so the unit tests
pass the trace's clock and UTC.

## Render errors

`error` is null when the node is drawn whole. Otherwise it is one or more
messages joined by `"; "`, and `view` says how much was drawn:

- `view: null`: the node has no view. The renderer inserts nothing for it (a
  one-child slot stays empty, an undrawn root gives the caller no root view) and
  reports the error. Never a blank stand-in, never a fallback.
- `view` set: the node is drawn, and the error names the part of it that is not.

The C++ returns every error with its JSON path (`RenderedTree::errors()`), so the
host can log or show it; the trace carries the same text on the node.

| node | condition | view | error |
|---|---|---|---|
| `markdown`, `tile`, `tabSlider`, `keyChips`, `mediaSession` | always | null | `not drawn in chapter 2` |
| `icon`, `iconButton` | `icon` is not a name of the [icon table](#icon-names) | null | `unknown icon: <name>` |
| `iconButton` | `variant` `prominent` or `floating` (stock `ImageButton` has one style) | drawn | `variant not drawn in chapter 2` |
| `button`, `listItem` | `icon` is not in the icon table | drawn, without the icon | `unknown icon: <name>` |
| `button` | `variant` `alert` or `accent` (stock `ui::ButtonStyle` has four styles) | drawn as `default` | `variant not drawn in chapter 2` |
| `select` | an option's `icon` is not in the icon table | drawn, the option without the icon | `unknown icon: <name>` |
| `select` | `label` is set (a `Combobox` has no label; it becomes the accessible name) | drawn | `label not drawn in chapter 2` |
| `switch` | `label` is set (a `ToggleButton` has no label; it becomes the accessible name) | drawn | `label not drawn in chapter 2` |
| `slider` | `icon`, `iconAction` or `toggled` (the icon's muted state) is set: they wait on the QuickSettingsSlider port | drawn, the bare `Slider` | `icon not drawn in chapter 2` |
| `badge` | `role` is set (stock `Badge` has no colour setter, rule R17) | drawn in the stock badge colours | `role not drawn in chapter 2` |
| `progress` | `shape` is `ring` (Views has no progress-ring view) | null | `ring not drawn in chapter 2` |
| `image` | `src` is not a relative path inside the plugin directory, is missing, or is not a PNG | null | `image outside the plugin directory: <src>`, `image not found: <src>`, `image is not a PNG: <src>` |
| radio `option` | the option has an `icon` (a `RadioButton`'s image is its indicator) | drawn without the icon | `icon not drawn in chapter 2` |

A non-string `icon` reads `unknown icon: <its text>`; a non-string `src` reads
`image src is not a path: <its text>`; an `image` rendered with no plugin
directory reads `image has no plugin directory: <src>`. `xdg:` names are never in
the table: rule R17 forbids an icon fallback chain, so a freedesktop name is not
looked up in an icon theme and is an unknown icon. The Python checks the PNG
signature; the C++ also decodes the file, so a PNG that is corrupt after its
signature fails only there (`image is not a PNG`).

## Not drawn in chapter 2

Five node types are in the schema but have no view yet. Each is rejected at
render time with `not drawn in chapter 2`, never drawn blank:

- `markdown`: a `views::StyledLabel` fed by a Markdown subset, with links that
  invoke commands or open URLs in Chrome. The subset grammar and the link routing
  (command vs URL, and who opens Chrome) are not written; StyledLabel alone is
  stock and ready.
- `tile`: the quick-settings tile face is Ash's `FeatureTile`, which waits on its
  port (A032). Views has no tile; drawing one from a button would be a new
  component, which rule R17 rules out.
- `tabSlider`: Ash's `TabSlider` (a segmented control of `MdTextButton`s with a
  sliding selection) is not ported. Composing one here would again be a new
  component.
- `keyChips`: the key chips are Ash's `KeyboardShortcutView` chips, not ported;
  Views has no chip.
- `mediaSession`: drawn by `components/global_media_controls`'
  `MediaItemUIView`, which brings `//components/media_message_center` and the
  media-session mojo interfaces into the closure, and needs an MPRIS source to
  bind. Neither the dependency closure nor the source exists in chapter 2.

Parts of drawn nodes that wait the same way are in [Render
errors](#render-errors): the slider's icon (QuickSettingsSlider), switch and
select labels, button and icon-button variants without a stock style, the badge
role and the progress ring.

## How the drawn view uses the props

Sizes and colours come from the kit only (rules R11, R17): `views::LayoutProvider`
metrics, the kit's `ShellTypographyProvider` line heights, and `ui::ColorId`s.
`shell/ui_tree/` contains no colour literal and no pixel constant.

| prop | drawn as |
|---|---|
| `spacing` `none`/`tight`/`normal`/`loose` | 0, `DISTANCE_RELATED_LABEL_HORIZONTAL`, `DISTANCE_RELATED_CONTROL_HORIZONTAL` / `_VERTICAL`, `DISTANCE_UNRELATED_CONTROL_HORIZONTAL` / `_VERTICAL` (horizontal in a row, vertical in a column; a grid pads its columns with the horizontal and its rows with the vertical one); a `spacer`'s `size` the same, both axes |
| `align` | `BoxLayout::CrossAxisAlignment` `kStart`, `kCenter`, `kEnd`, `kStretch` |
| `container` | `views::CreateRoundedRectBackground(kColorSysSurface2, …)` with the `Emphasis::kMedium` corner radius on all, the top or the bottom corners, and `INSETS_DIALOG_SUBSECTION` inside |
| `flex` | `BoxLayoutView::SetFlexForView` in a `row` or `column`; ignored in other parents |
| `typography` | `display` `STYLE_HEADLINE_1`, `title` `STYLE_HEADLINE_4`, `headline` `STYLE_HEADLINE_5`, `body` `STYLE_BODY_3`, `body-strong` `STYLE_BODY_3_MEDIUM`, `button` `STYLE_BODY_4_MEDIUM`, `annotation` `STYLE_BODY_5`, `label` `STYLE_CAPTION_MEDIUM`, in `CONTEXT_LABEL` |
| `role` | `default` `kColorSysOnSurface`, `subtle` `kColorSysOnSurfaceSubtle`, `primary` `kColorSysPrimary`, `alert` `kColorSysError`, `disabled` `kColorSysStateDisabled`; `positive` `kColorAlertLowSeverity` and `warning` `kColorAlertMediumSeverityText` (icons `…Icon`), because ui/color has no kColorSys success or warning role (style/theme-map.json `unmapped_by_design`) |
| icon `size` | the line height of `STYLE_BODY_5` (`small`), `STYLE_BODY_3` (`medium`), `STYLE_HEADLINE_4` (`large`); an icon button adds `xsmall` `STYLE_CAPTION` and `xlarge` `STYLE_HEADLINE_1` |
| image `size` | 2, 3, 4 or 6 times the large icon size (`thumbnail`, `small`, `medium`, `large`); `rounded` clips the layer to the `Emphasis::kMedium` radius |
| scroll `maxHeight` | `ClipHeightTo(0, h)`: `DISTANCE_DIALOG_SCROLLABLE_AREA_MAX_HEIGHT` halved (`small`), as is (`medium`), `DISTANCE_MODAL_DIALOG_SCROLLABLE_AREA_MAX_HEIGHT` (`large`); `surface` does not clip |
| textfield `size` | `SetDefaultWidthInChars` 12, 24 or 40 |
| `visible`, `enabled`, `tooltip`, `accessibleName` | `SetVisible`, `SetEnabled`, `SetTooltipText`, the accessible name; a value of another kind leaves the default |
| `id` | not drawn: it names the node for the plugin |
| a `repeat`'s own `visible`, `enabled`, `tooltip`, `accessibleName` | applied only when the repeat has a view (a one-child slot); in a list its copies stand in the parent and carry their own |
| `image` `src` | read and decoded synchronously when the node is built (and again when `src` changes, which `Rebind` treats as a structure change) |

Interactions call `Delegate::OnAction` with the trace's `{command, args, confirm,
close}`, resolved against the latest snapshot. Inputs add their value to `args`
under `value`: `switch` and `checkbox` a boolean, `radioGroup` and `select` the
option value, `slider` a number in 0–1 (a drag fires once, at its end; keyboard
and click changes fire at once), `textfield` the text on Enter. `confirm` and
`close` are the host's to honour (a `ui::DialogModel` confirmation, closing the
surface); the renderer only reports them.

## Icon names

The names views-shell draws, each a stock `gfx::VectorIcon` from
`ui/views/vector_icons` (`views::`) or `components/vector_icons`
(`vector_icons::`). This table is data: `tools/ui-tree-render.py` reads it, and
`views_shell_unittests` checks the compiled table in
`shell/ui_tree/ui_tree_renderer.cc` against it row for row. A name outside it is
a render error (`unknown icon`), never a substitute.

Names the examples use with no stock twin are not here and render as errors
today: `power`, `logout`, `suspend`, `hibernate`, `music-note`, `brightness`,
`network-wifi-strong`, `network-wifi-weak`, `battery-70`. Their glyphs are in
Ash's vector icons (`system_power_button_menu_*`, `shelf_logout`,
`unified_menu_brightness*`, `unified_menu_wifi*`, `unified_menu_battery*`,
`music_note`) and wait on the ported Ash subset (views-shell.pak).

| name | icon |
|---|---|
| `bluetooth` | `vector_icons::kBluetoothIcon` |
| `bluetooth-connected` | `vector_icons::kBluetoothConnectedIcon` |
| `bluetooth-disabled` | `vector_icons::kBluetoothDisabledIcon` |
| `bluetooth-searching` | `vector_icons::kBluetoothSearchingIcon` |
| `cast` | `vector_icons::kCastIcon` |
| `check` | `views::kCheckIcon` |
| `close` | `vector_icons::kCloseIcon` |
| `desktop` | `vector_icons::kDesktopWindowsIcon` |
| `devices` | `vector_icons::kDevicesIcon` |
| `do-not-disturb` | `vector_icons::kNotificationsOffIcon` |
| `error` | `vector_icons::kErrorIcon` |
| `globe` | `vector_icons::kGlobeIcon` |
| `headset` | `vector_icons::kHeadphonesIcon` |
| `help` | `vector_icons::kHelpIcon` |
| `home` | `vector_icons::kHomeIcon` |
| `info` | `vector_icons::kInfoIcon` |
| `keyboard` | `vector_icons::kKeyboardIcon` |
| `lock` | `vector_icons::kLockIcon` |
| `mic` | `vector_icons::kMicIcon` |
| `mic-off` | `vector_icons::kMicOffIcon` |
| `more` | `views::kMoreHorizIcon` |
| `mouse` | `vector_icons::kTouchpadMouseIcon` |
| `network-wired` | `vector_icons::kSettingsEthernetIcon` |
| `notifications` | `vector_icons::kNotificationsIcon` |
| `open-in-new` | `views::kOpenInNewIcon` |
| `pause` | `vector_icons::kPauseIcon` |
| `play` | `vector_icons::kPlayArrowIcon` |
| `refresh` | `vector_icons::kRefreshIcon` |
| `restart` | `vector_icons::kReplayIcon` |
| `search` | `vector_icons::kSearchIcon` |
| `settings` | `vector_icons::kSettingsIcon` |
| `skip-next` | `vector_icons::kSkipNextIcon` |
| `skip-previous` | `vector_icons::kSkipPreviousIcon` |
| `speaker` | `vector_icons::kVolumeUpIcon` |
| `tune` | `vector_icons::kTuneIcon` |
| `usb` | `vector_icons::kUsbIcon` |
| `volume-medium` | `vector_icons::kVolumeUpIcon` |
| `volume-muted` | `vector_icons::kVolumeOffIcon` |
| `warning` | `vector_icons::kWarningIcon` |

## Decisions made here

These close points the schemas left open; change them only with the fixtures.

1. A `repeat` has no view of its own except in a one-child slot (above).
2. Trace nodes always carry all six keys; `layout` names the layout manager for
   containers (`box-vertical`, `box-horizontal`, `fill`, `table`).
3. Node-valued properties are `children`, never `props`.
4. Unresolved bindings are marked in place, not dropped.
5. Bare commands are qualified with the plugin id in both the trace and the
   registry view.
6. The registry view also lists `quickSettings`, `dependencies`
   (`requires.plugins`) and, per command, `icon` and `category`, which the
   brief's field list left out: T1 quick-settings plugins contribute nothing
   else, and launcher and menu rows need the icon.
7. Activation is a sorted set of `on<Kind>:<id>` events; bar, bar-widget and
   service surfaces give `onStartup` (the plugin tool's docstring lists all
   kinds).
8. A `keyFrom` binding resolves to its configuration property's default; the
   user's configuration overrides it at run time, outside this view.
9. A node chapter 2 cannot draw, or draws only in part, says so in `error`
   (Render errors); `view` is null exactly when nothing is drawn for it (w3a).
10. Icon names are the table above and nothing else (w3a, rule R17).
11. `time` and `relative-time` accept only the ISO 8601 instant form above, so
    the Python (`datetime.fromisoformat`) and the C++ agree on what parses (w3a).
