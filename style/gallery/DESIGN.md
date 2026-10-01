# views-shell/style gallery: design

The gallery shows every `views-shell/style` component in every variant and state, in the
theme you pass (any Omarchy theme directory; `noir` is the default example). It serves two jobs at once:

1. **Showcase.** The page you open to see the kit, and the one screenshot that
   explains the look.
2. **Conformance harness.** The same grids are rendered under a headless
   compositor at each Chromium hop and compared against the previous hop's
   images. A component whose pixels change is flagged for reading, not failed.

## Upstream models

| Model | What views-shell takes |
|---|---|
| `ui/views/examples/` (`ExampleBase`, `ExamplesWindowContents`, `CreateExamples()`) | **The structure.** Each component page is an example class with a title and a `CreateExampleView`, registered in one table. The stock-control pages already exist upstream (`button_example`, `textfield_example`, `table_example`, …), and the kit is mostly those controls. |
| `ui/views/examples` Views Canvas and `JsonViewBuilder` (commits `4bc290a2923f`, `384eb2a46f11`, `a3c4567c8647`) | A JSON-driven page: one page per `ui` tree node type, built from fixture trees, so the gallery also shows what a plugin can draw. Whether views-shell links, copies or only follows it is open (P9). |
| `ash/style/style_viewer/` (at 154.0.8037.92) | Pattern only: variants as rows and states as columns, one factory per component. Used for the two ported quick-settings faces (FeatureTile, QuickSettingsSlider) and the key chips. |

## Shape

- **Surface.** One overlay layer surface, namespace `views-shell-gallery`, keyboard
  exclusive, no exclusive zone. Rule R1 holds for the gallery too: it is never an
  `xdg_toplevel`. It opens with `views-shell gallery` and closes with Esc.
- **Left column.** A list of components, grouped as in
  [`../INVENTORY.md`](../INVENTORY.md): stock controls, theming, the five ports.
- **Right pane.** A `ScrollView` holding the selected component's grid.
- **Grid.** Rows are variants (for example MdTextButton's default, tonal,
  prominent and text styles, each with and without an icon). Columns are states: normal, hovered, pressed, focused, disabled, and
  toggled where it applies.
- **Typography page.** Every `views::style` text context and style the views-shell
  `TypographyProvider` resolves, with its size, weight and line height.
- **Colour page.** Every `kColorSys*` id pinned by
  [`../theme-map.json`](../theme-map.json), and the unpinned seed-derived roles,
  as swatches with the id, the Omarchy key it comes from and the resolved value.

## Code shape (sketch)

```cpp
// views-shell/style/gallery/gallery_factories.h
namespace views-shell::gallery {

using GridFactory = std::unique_ptr<views::View> (*)();

struct GalleryEntry {
  std::u16string_view group;
  std::u16string_view title;
  GridFactory create;
};

// One line per component, in INVENTORY order.
base::span<const GalleryEntry> Entries();

std::unique_ptr<views::View> CreateMdTextButtonGrid();
std::unique_ptr<views::View> CreateImageButtonGrid();
std::unique_ptr<views::View> CreateFeatureTileGrid();
// ...

}  // namespace views-shell::gallery
```

Each factory builds a `GridView` from (variant × state) cells. State is forced
programmatically (`SetState`, `SetEnabled`, `RequestFocus`), never by synthetic
input, so that the headless capture is deterministic.

## Conformance run

Under `runtime-test`, inside a nested headless compositor:

```
views-shell gallery --headless --capture <dir>
```

writes one PNG per component page and a `manifest.json` with each image's sha256.
The hop ledger records the list of pages whose hash changed.

## Not in the gallery

UI-tree rendering of plugin trees is tested separately, with fixture trees from
[`../../schemas/`](../../schemas/) rendered to images. The gallery shows
components, not plugins.
