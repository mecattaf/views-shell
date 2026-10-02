# shell/ui_tree: the ui-tree renderer

Layer 7 of [`../../docs/architecture.md`](../../docs/architecture.md): a T1 or T2
plugin's `ui` tree ([`../../schemas/ui-tree.schema.json`](../../schemas/ui-tree.schema.json))
drawn with stock Views, with no colour literal and no pixel size (rules R11,
R17). The contract is [`../../schemas/ui-tree-rendering.md`](../../schemas/ui-tree-rendering.md);
its Python reference, [`../../tools/ui-tree-render.py`](../../tools/ui-tree-render.py),
and the C++ here produce the same render trace byte for byte, and
`views_shell_unittests` holds the C++ to the committed goldens.

| file | what it is |
|---|---|
| `ui_tree.{h,cc}` | `ParseUiTree` / `ParseUiTreeJson`: the schema checked in C++ (no JSON Schema engine at 154). Every node type of the schema becomes a typed `UiNode`; an unknown node or property, a missing required one, a value of the wrong kind or a malformed binding or command is a `ParseError` with its JSON path. |
| `binding.{h,cc}` | `BindingResolver`: `{"$bind"}` JSON Pointers (RFC 6901, `./` inside a repeat, `source` read from `Snapshot::sources`), the format table (`percent`, `duration`, `bytes`, `time`, `relative-time`, `text`) with an injectable clock and zone (`FormatClock`), and the Python-identical JSON writer. |
| `ui_tree_renderer.{h,cc}` | `RenderUiTree` and `RenderedTree`: the view table, the icon table (`FindIcon`), render errors, actions to a `UiTreeDelegate`, `Rebind`. |
| `list_item_view.{h,cc}` | `views_shell::ListItemView`, the `listItem` row: a `views::Button` of `ImageView`, two `Label`s and a trailing view. |
| `render_trace.{h,cc}` | `RenderTrace` / `SerializeTrace`: the trace of what was built, the view class read from each view. |
| `*_unittest.cc` | `UiTreeParseTest`, `BindingTest`, `BindingFormatTest`, `RenderTraceTest` (one per examples ui file), `UiTreeRendererTest`, `ListItemViewTest`. |

## Using it

```cpp
namespace ut = views_shell::ui_tree;

auto tree = ut::ParseUiTreeJson(json);            // or ParseUiTree(base::Value)
if (!tree.has_value()) {
  LOG(ERROR) << tree.error().ToString();          // "/root/children/2/role: ..."
  return;
}
ut::RenderOptions options;
options.plugin_id = "example.power-menu";         // qualifies bare commands
options.plugin_dir = plugin_directory;            // where image src files live
// options.clock defaults to the real clock and the local zone.
std::unique_ptr<ut::RenderedTree> rendered = ut::RenderUiTree(
    std::move(tree).value(), std::move(snapshot), options, delegate);
for (const ut::RenderError& e : rendered->errors()) {
  LOG(WARNING) << e.path << ": " << e.message;    // undrawn nodes and parts
}
if (std::unique_ptr<views::View> root = rendered->TakeRootView()) {
  surface->AddChildView(std::move(root));
}
...
// A new snapshot from the plugin (T2 `snapshot`) or the entry's state (T1):
if (rendered->Rebind(std::move(next)) !=
    ut::RenderedTree::RebindResult::kInPlace) {
  // The structure changed (repeat items or keys, radio options, which nodes
  // are drawn): render the tree again.
}
```

`delegate->OnAction(const UiAction&)` receives `{command, args, confirm, close,
path}`: the command qualified with the plugin id, the args resolved against the
latest snapshot plus, for an input, its new value under `value`. Asking for
confirmation and closing the surface are the host's job. The `RenderedTree`
must stay alive while its views take input; if it goes first, the views'
callbacks are no-ops (they hold a weak pointer), and if the root view goes
first, `Rebind` answers `kNoViews`.

`snapshot` is a `ut::Snapshot`: `state` (the entry's state or the T2 snapshot)
and `sources` (`"<plugin>/<source>"` to that source's snapshot), the shape of
`tools/fixtures/snapshots/*.json` (`Snapshot::FromJson`).

## What chapter 2 does not draw

The five node types `markdown`, `tile`, `tabSlider`, `keyChips` and
`mediaSession`, icon names outside the icon table (including every `xdg:` name),
and the parts listed under "Render errors" in the contract are reported, never
drawn blank. The icon table holds only stock `ui/views` and
`components/vector_icons` icons; the examples' `power`, `logout`, `suspend`,
`hibernate`, `music-note`, `brightness`, `network-wifi-*` and `battery-*` wait on
the ported Ash icon subset.

## Tests

Views tests need aura::Env, which on this Wayland-only build connects to a
compositor. Without `WAYLAND_DISPLAY` the same views are built and traced
outside a Widget; with one (on the bench, inside `runtime-test` against a
private headless scroll: [`../../tools/bench/seq/w3a.sh`](../../tools/bench/seq/w3a.sh),
stage `display`) every rendered root is shown in a real Widget.
