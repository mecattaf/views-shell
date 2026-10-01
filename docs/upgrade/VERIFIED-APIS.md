# VERIFIED-APIS.md — src/agency/shell (Surface 1: top bar)

Every Chromium symbol the shell code and `patches/agency-shell-host.patch` use,
verified against the **live 150 tree** on the worker
(`ssh -o BatchMode=yes tom@10.77.0.2 'bash -c "cd ~/chromium/src && git show HEAD:<path>"'`),
not from memory. Two-clocks note: the rescued substrate was cut against 148/149;
these verifications are against live 150. Where the design (`topbar-design.md`)
named an API that 150 has since renamed, the **real 150 signature wins** and the
adaptation is recorded below.

## views::Widget — `ui/views/widget/widget.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `enum Ownership { CLIENT_OWNS_WIDGET, NATIVE_WIDGET_OWNS_WIDGET, WIDGET_OWNS_NATIVE_WIDGET }` | widget.h:271,284,292,304 | shell_host.cc |
| `explicit InitParams(Ownership, Type = TYPE_WINDOW)` | widget.h:319 | shell_host.cc |
| `enum Type { TYPE_WINDOW, ... }` | widget.h:236 | shell_host.cc |
| `std::string name;` (InitParams field) | widget.h:351 | shell_host.cc (weld key) |
| `gfx::Rect bounds;` (InitParams field) | widget.h:449 | shell_host.cc |
| `enum class WindowOpacity { ..., kTranslucent }` + `WindowOpacity opacity;` | widget.h:249,260,366 | shell_host.cc |
| `void Init(InitParams params);` | widget.h:654 | shell_host.cc |
| `T* SetContentsView(std::unique_ptr<T>);` | widget.h:728 | shell_host.cc |
| `void Show();` | widget.h:939 | shell_host.cc |

Ownership choice: **CLIENT_OWNS_WIDGET** (widget.h:106 "All widgets should use
ownership = CLIENT_OWNS_WIDGET"); ShellHost holds `std::unique_ptr<views::Widget>`.

## views::View — `ui/views/view.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `virtual gfx::Size CalculatePreferredSize(const SizeBounds& available_size) const;` | view.h:1846-1847 | bar_view.{h,cc} (override) |
| `T* AddChildView(std::unique_ptr<T>);` | view.h:466 | bar_view.cc |
| `LayoutManager* SetLayoutManager(std::unique_ptr<LayoutManager>);` (templated, returns the manager) | view.h (SetLayoutManager present) | bar_view.cc |
| `void SetProperty(...)` / `SetBackground(...)` (View base helpers) | view.h | bar_view.cc |

`SizeBounds` is forward-declared in view.h:121 (`namespace views`); full definition
+ `SizeBound::is_bounded()` / `value()` in `ui/views/layout/layout_types.h`:36,55,58
and `SizeBounds` at layout_types.h:109. `available_size.width()` returns a `SizeBound`.

## views::Label — `ui/views/controls/label.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `explicit Label(std::u16string_view text = {});` | label.h:72 | bar_view.cc |
| `virtual void SetText(std::u16string_view text);` | label.h:102 | clock_controller.cc |
| `virtual void SetEnabledColor(ui::ColorVariant color);` | label.h:136 | bar_view.cc |

**ADAPTATION (design → 150):** `topbar-design.md` §3.2 named
`Label::SetEnabledColorId(ui::kColorSysOnSurface)`. At 150 the method is
`SetEnabledColor(ui::ColorVariant)`. `ui::ColorVariant` is *implicitly*
constructed from a `ui::ColorId` (color_variant.h:30 `ColorVariant(ColorId)`),
so `SetEnabledColor(ui::kColorSysOnSurface)` is token-clean and equivalent. The
adaptation is producer-truth-wins: the shell adapts to the real header.

## views background — `ui/views/background.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `std::unique_ptr<Background> CreateSolidBackground(ui::ColorVariant color);` | background.h:88 | bar_view.cc |

**ADAPTATION (design → 150):** `topbar-design.md` §3.2 named
`CreateThemedSolidBackground(ui::kColorSysHeader)`. At 150 that free function no
longer exists; the themed path is merged into `CreateSolidBackground(ui::ColorVariant)`
(background.h stores `ui::ColorVariant color_;`). Passing the `ui::ColorId`
`ui::kColorSysHeader` yields a fully themed, token-clean background. No raw hex.

## ui::ColorVariant / ColorId — `ui/color/color_variant.h`, `ui/color/color_id.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `ColorVariant(ColorId color_id);` (implicit) | color_variant.h:30 | bar_view.cc |
| `E_CPONLY(kColorSysHeader)` | color_id.h:173 | bar_view.cc (bar bg) |
| `E_CPONLY(kColorSysOnSurface)` | color_id.h:136 | bar_view.cc (clock text) |
| fallbacks `kColorPrimaryBackground` (color_id.h:266), `kColorLabelForeground` (color_id.h:383) | present | documented fallbacks |

Both primary M3 tokens exist at 150 — **no fallback needed**; the design's
fallback ids are recorded but unused.

## FlexLayout — `ui/views/layout/flex_layout.h`, `flex_layout_types.h`, `layout_types.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `class FlexLayout : public LayoutManagerBase` | flex_layout.h:72 | bar_view.cc |
| `FlexLayout& SetOrientation(LayoutOrientation);` | flex_layout.h:87 | bar_view.cc |
| `FlexLayout& SetMainAxisAlignment(LayoutAlignment);` | flex_layout.h:88 | bar_view.cc |
| `FlexLayout& SetCrossAxisAlignment(LayoutAlignment);` | flex_layout.h:89 | bar_view.cc |
| `class FlexSpecification` | flex_layout_types.h:125 | bar_view.cc |
| `explicit FlexSpecification(MinimumFlexSizeRule, MaximumFlexSizeRule = kPreferred, bool = false);` | flex_layout_types.h:144 | bar_view.cc |
| `FlexSpecification WithWeight(int) const;` | flex_layout_types.h:174 | bar_view.cc |
| `enum class MinimumFlexSizeRule { kScaleToZero, ... }` | flex_layout_types.h:51-52 | bar_view.cc |
| `enum class MaximumFlexSizeRule { kPreferred, kScaleToMaximum, kUnbounded }` | flex_layout_types.h:62-65 | bar_view.cc |
| `kFlexBehaviorKey` (ClassProperty<FlexSpecification*>) | flex_layout_types.h:77 | bar_view.cc |
| `enum class LayoutOrientation { kHorizontal, ... }` | layout_types.h:29-30 | bar_view.cc |
| `enum class LayoutAlignment { kStart, kCenter, kEnd, kStretch, kBaseline }` | layout_types.h:26 | bar_view.cc |

## base — time / timer / command line

| Symbol | Verified @150 | Used in |
|---|---|---|
| `std::u16string TimeFormatTimeOfDay(const Time& time);` — `base/i18n/time_formatting.h:29` | verified | clock_controller.cc |
| `class RepeatingTimer : public internal::DelayTimerBase;` + `void Start(const Location&, TimeDelta, Receiver*, Method);` — `base/timer/timer.h:264,292` | verified | clock_controller.{h,cc} |
| `base::CommandLine::ForCurrentProcess()->HasSwitch(...)` — `base/command_line.h` | standard, matches stage2 weld recipe | shell_browser_main_extra_parts.cc |

`TimeFormatTimeOfDay` lives in the **`//base:i18n`** component (base/BUILD.gn:3079
`component("i18n")`, sources include `i18n/time_formatting.{cc,h}` at :3114-3115)
— hence `//base:i18n` in the shell's `deps`.

## mojo — `mojo/public/cpp/bindings/receiver.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `class Receiver;` | receiver.h:44 | clock_controller.h |
| `PendingRemote<Interface> BindNewPipeAndPassRemote();` | receiver.h:128 | clock_controller.cc |

## ChromeBrowserMainExtraParts — `chrome/browser/chrome_browser_main_extra_parts.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `virtual void PostBrowserStart() {}` (empty default) | extra_parts.h | shell_browser_main_extra_parts.{h,cc} |
| `virtual void PostMainMessageLoopRun() {}` (empty default) | extra_parts.h | shell_browser_main_extra_parts.{h,cc} |
| `virtual ~ChromeBrowserMainExtraParts() = default;` | extra_parts.h | base class |

Header is exported by the source_set **`//chrome/browser:main_extra_parts`**
(chrome/browser/BUILD.gn:143, `sources = [ "chrome_browser_main_extra_parts.h" ]`)
— hence that target in the shell's `public_deps`.

## chrome/browser hook sites (for patches/agency-shell-host.patch)

| Fact | Verified @150 |
|---|---|
| `main_parts->AddParts(...)` registration lives inside `#if defined(TOOLKIT_VIEWS)` at chrome_browser_main.cc:770-777 (Linux branch adds `ChromeBrowserMainExtraPartsViewsLinux`) | verified; the agency AddParts is inserted immediately after that block's closing `#endif` (line 777) |
| `ChromeBrowserMainParts::AddParts(std::unique_ptr<ChromeBrowserMainExtraParts>)` exists | chrome_browser_main.cc:2285 |
| The gn target compiling chrome_browser_main.cc is `source_set("core")` | chrome/browser/BUILD.gn:605, sources include `chrome_browser_main.cc` at :618 |
| `core`'s deps list begins at BUILD.gn:659 (`deps = [ ":active_use_util", ...`); `:browser_public_dependencies` (:668) is unique to `core` (the sibling `static_library("browser")` at :1560 lacks it), used to disambiguate the patch anchor | verified |
| Include anchor: the platform extra-parts include block closes at chrome_browser_main.cc:341 (`#endif` after `chrome_browser_main_extra_parts_ozone.h`) | verified |

## buildflag

`BUILDFLAG(ENABLE_COWL_LAYER_SHELL)` is emitted by `//agency/build:cowl_buildflags`
at include path `cowl/build/cowl_buildflags.h` (src/agency/build/BUILD.gn
`buildflag_header("cowl_buildflags") { header_dir = "cowl/build" }`). The patch
adds `//agency/build:cowl_buildflags` to `//chrome/browser:core` deps so
chrome_browser_main.cc can resolve the header and read the flag. `cowl_buildflags.h`
is included **unconditionally** (it defines the macro the `#if` guard reads).

# Surface 3 (launcher) — additions

Every NEW Chromium/fork symbol the launcher code
(`launcher_panel.{h,cc}`, `shell_host.{h,cc}` BuildLauncher, the weld row) uses,
verified against the **live 150 tree** on the worker
(`ssh -o BatchMode=yes tom@10.77.0.2 'bash -c "cd ~/chromium/src && git show HEAD:<path>"'`),
except the fork layer-shell enums which are verified against
`patches/agency-source.patch` (they do not exist in pristine 150).

## views::TextfieldController — `ui/views/controls/textfield/textfield_controller.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `virtual void ContentsChanged(Textfield* sender, const std::u16string& new_contents) {}` | textfield_controller.h:38-39 | launcher_panel.{h,cc} (override) |
| `virtual bool HandleKeyEvent(Textfield* sender, const ui::KeyEvent& key_event);` | textfield_controller.h:44 | launcher_panel.{h,cc} (override) |

`ContentsChanged` has an empty `{}` default (not pure) — override is well-formed.

## views::Textfield — `ui/views/controls/textfield/textfield.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `void SetController(TextfieldController* controller);` | textfield.h:133 | launcher_panel.cc |
| `void SetText(std::u16string_view new_text);` | textfield.h:158 | launcher_panel.cc |
| `void SetTextColorId(std::optional<ui::ColorId> color_id);` | textfield.h:217 | launcher_panel.cc |
| `void SetBackgroundColor(std::optional<ui::ColorVariant> color);` | textfield.h:224 | launcher_panel.cc |
| `void SetCursorEnabled(bool enabled);` | textfield.h:248 | launcher_panel.cc |
| `void SetFontList(const gfx::FontList& font_list);` | textfield.h:252 | launcher_panel.cc |
| `void SetBorder(std::unique_ptr<Border> b) override;` | textfield.h:363 | launcher_panel.cc |

**ADAPTATION (rescued 148/149 → 150):** the rescued panel called
`SetTextColor(SkColor)`. At 150 that setter is gone; the token-clean
`SetTextColorId(std::optional<ui::ColorId>)` (textfield.h:217) replaces it —
so `search_->SetTextColorId(ui::kColorSysOnSurface)` is *more* token-clean than
the rescued raw-color call. `SetBackgroundColor` now takes
`std::optional<ui::ColorVariant>`; `SK_ColorTRANSPARENT` (an `SkColor`) flows in
via the implicit `ColorVariant(SkColor)` ctor (color_variant.h:29) → `optional`.

## views::View — additional (`ui/views/view.h`)

| Symbol | Verified @150 | Used in |
|---|---|---|
| `void RemoveAllChildViews();` | view.h:541 | launcher_panel.cc (ClearRows) |
| `void InvalidateLayout(bool = false);` | view.h:876 | launcher_panel.cc (RebuildResults) |

## views::Label — additional (`ui/views/controls/label.h`)

| Symbol | Verified @150 | Used in |
|---|---|---|
| `void SetAutoColorReadabilityEnabled(bool enabled);` | label.h:130 | launcher_panel.cc |
| `void SetHorizontalAlignment(gfx::HorizontalAlignment alignment);` | label.h:178 | launcher_panel.cc |

## views background — additional (`ui/views/background.h`)

| Symbol | Verified @150 | Used in |
|---|---|---|
| `CreateRoundedRectBackground(ui::ColorVariant, float radius, int for_border_thickness = 0);` | background.h:111 | launcher_panel.cc (panel/icon/chip/keycap) |
| `CreateRoundedRectBackground(ui::ColorVariant, const gfx::RoundedCornersF&, const gfx::Insets&);` | background.h:130 | launcher_panel.cc (selection highlight) |
| `CreateSolidBackground(ui::ColorVariant);` | background.h:88 | launcher_panel.cc (separator) |

## ui::ColorId — additional launcher tokens (`ui/color/color_id.h`)

Every opaque launcher color is one of these M3 semantic ids (token-clean):

| Token | Verified @150 | Maps rescued raw | Used for |
|---|---|---|---|
| `kColorSysOnSurface` | color_id.h:136 | `kPrimaryText` (white) | search text, row primary label |
| `kColorSysOnSurfaceSubtle` | color_id.h:158 | `kSecondaryText`/`kChipText` | detail text, chip/keycap text, footer |
| `kColorSysNeutralContainer` | color_id.h:167 | `kChipBg` | chip + keycap background |
| `kColorSysDivider` | color_id.h:168 | `kSeparator` | the 1px separator |
| `kColorSysStateHoverOnSubtle` | color_id.h:183 | `kSelectedRow` | selected-row highlight |
| `kColorSysTonalContainer` | color_id.h:161 | (alternate for chip bg) | recorded, unused |
| `kColorSysBaseContainerElevated` | color_id.h:172 | (option (b) glass) | recorded, unused |

`ui::ColorVariant(ColorId)` is implicit (color_variant.h:30), so a `ColorId`
flows into every `CreateSolidBackground` / `CreateRoundedRectBackground` /
`Label::SetEnabledColor` call unchanged.

### The ONE documented raw-color exception — `kLauncherGlassFill`

`kLauncherGlassFill = SkColorSetARGB(0xCC, 0x1c, 0x1c, 0x1e)` — the frosted-glass
panel fill (launcher_panel.cc). Supervisor-recommended **option (a)**
(launcher-design.md §3): its alpha is load-bearing for niri blur-through (spike
signal #2) and no opaque M3 semantic token provides partial transparency. This
single named constant is the deliberate, isolated exception; every other color
in the panel is a ColorId above. Flows into `CreateRoundedRectBackground` via
`ColorVariant(SkColor)` (color_variant.h:29). Icon swatches (`kSwatchPalette`)
are icon-IDENTITY colors (favicon-like), inherently non-semantic, so also raw by
nature (delta D4).

## base — launcher bring-up (`base/environment.h`, `base/nix/xdg_util.h`, `base/strings/utf_string_conversions.h`)

| Symbol | Verified @150 | Used in |
|---|---|---|
| `static std::unique_ptr<Environment> Create();` | environment.h:34 | shell_host.cc |
| `virtual std::optional<std::string> GetVar(cstring_view variable_name) = 0;` | environment.h:39 | shell_host.cc (`.value_or(...)`) |
| `BASE_EXPORT FilePath GetXDGDirectory(Environment* env, const char* env_name, const char* fallback_dir);` | xdg_util.h:95 | shell_host.cc |
| `[[nodiscard]] std::string UTF16ToUTF8(std::u16string_view utf16);` | utf_string_conversions.h:49 | launcher_panel.cc (Query arg) |
| `[[nodiscard]] std::u16string UTF8ToUTF16(std::string_view utf8);` | utf_string_conversions.h:45 | launcher_panel.cc (row labels) |

## ui events — nav keys (`ui/events/...`)

| Symbol | Verified @150 | Used in |
|---|---|---|
| `EventType::kKeyPressed` | event_type.h:19 | launcher_panel.cc |
| `VKEY_RETURN=0x0D`, `VKEY_ESCAPE=0x1B`, `VKEY_UP=0x26`, `VKEY_DOWN=0x28` | keyboard_codes_posix.h:50,63,74,76 | launcher_panel.cc |

## gfx — `ui/gfx/geometry/rounded_corners_f.h`, `ui/gfx/font_list.h`

| Symbol | Verified @150 | Used in |
|---|---|---|
| `constexpr explicit RoundedCornersF(float all);` | rounded_corners_f.h:25 | launcher_panel.cc (highlight) |
| `FontList DeriveWithSizeDelta(int size_delta) const;` | font_list.h:104 | launcher_panel.cc |

## Fork layer-shell enums (weld row) — `patches/agency-source.patch`

Verified against the fork source patch (these do NOT exist in pristine 150):

| Symbol | Verified | Weld row value |
|---|---|---|
| `enum class LayerShellLayer { ..., kOverlay = 3 }` | agency-source.patch:532,536 | `ui::LayerShellLayer::kOverlay` |
| `enum class LayerShellKeyboardInteractivity { ..., kExclusive = 1 }` | agency-source.patch:539,541 | `ui::LayerShellKeyboardInteractivity::kExclusive` |
| `kLayerShellAnchorNone = 0` | agency-source.patch:547 | `ui::kLayerShellAnchorNone` |

`PlatformWindowType::kLayerShell` and the `layer_shell_{layer,anchor,exclusive_zone,keyboard_interactivity,namespace}`
`PlatformWindowInitProperties` fields are already exercised by the pre-existing
`agency:bar` / `BrowserWidget` rows in the same table — the launcher row adds no
new property, only the three enum *values* above.

## LauncherProducer API (agency, not chromium) — `agency/producers/launcher/{launcher_producer,launcher_result}.h`

Bound by direct in-process virtual dispatch (no mojom). Verified against the
in-tree headers:

| Symbol | Header | Used in |
|---|---|---|
| `LauncherProducer(base::FilePath, std::string, std::vector<std::string>);` | launcher_producer.h:65-67 | shell_host.cc |
| `void Initialize();` | launcher_producer.h:83 | shell_host.cc |
| `std::vector<LauncherResult> Query(const std::string& query);` | launcher_producer.h:90 | launcher_panel.cc |
| `void Activate(const std::string& provider_id, const std::string& result_id);` | launcher_producer.h:100 | launcher_panel.cc |
| `struct LauncherResult { provider_id, result_id, title, details, icon, relevance; }` | launcher_result.h:34-71 | launcher_panel.cc |

## Patch verification evidence

`patches/agency-shell-host.patch` was cut with difflib discipline (unified diff,
n=3) against the pristine HEAD:150 content of both files
(`before.replace(EXACT_BLOCK, NEW_BLOCK, 1)`, `assert count == 1`), then
`git apply --check` verified in a scratch git repo seeded with the pristine files:

```
Checking patch chrome/browser/chrome_browser_main.cc...
Checking patch chrome/browser/BUILD.gn...
APPLY CHECK: OK
```

`patches/agency-layer-shell-weld.patch` (launcher row) was re-cut on top of the
`agency-browser-fullbleed`-gated version (merged to main as `d626cdd`, per the
coordination rule: `git merge origin/main`, confirmed
`grep -c agency-browser-fullbleed = 1` before editing). The `agency:launcher`
row (`kOverlay`, `kLayerShellAnchorNone`, `exclusive_zone=0`, `kExclusive`,
ns `agency-launcher`) was inserted after the `BrowserWidget` row and the
**entire dwthl.cc hunk was regenerated** with difflib discipline (unified diff,
n=3) between pristine HEAD:150 and the fullbleed-plus-launcher version
(`assert after.count(ANCHOR) == 1`), leaving the other two file sections
(opaque_browser_frame_view.cc, tab_drag_controller.cc) byte-identical.
`git apply --check` (and a full `git apply`) verified in a scratch git repo
seeded with the three pristine HEAD:150 files:

```
Checking patch ui/views/widget/desktop_aura/desktop_window_tree_host_linux.cc...
Checking patch chrome/browser/ui/views/frame/opaque_browser_frame_view.cc...
Checking patch chrome/browser/ui/views/tabs/dragging/tab_drag_controller.cc...
APPLY CHECK: OK (0 FAILED, 0 --3way); 'agency:launcher' present in output.
```
