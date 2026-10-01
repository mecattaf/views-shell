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

`tile` waits on the FeatureTile port (A032). The undrawn nodes still carry their
resolved `props`, so the trace already says what the face will receive.

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
  its `format` and `source`.

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
| `time` | epoch seconds, or an ISO 8601 string with `Z` or an offset | `HH:MM`, 24-hour, in UTC |
| `relative-time` | as `time` | against the fixed clock `2026-10-02T00:00:00Z`: under 60 s `now`; else `N min`, `N h` or `N d` (floored) followed by `ago`, or preceded by `in` for a future instant |

UTC and the fixed clock keep the trace deterministic. The drawn shell formats
`time` and `relative-time` in the local zone against the real clock; the trace
is the shape of that call, not its wording.

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
