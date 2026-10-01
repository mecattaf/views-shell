# Tokens: the theme you pass

views-shell has no built-in colour scheme. Its colours come from the theme you pass, and
the theme format is **exactly Omarchy's theme input**: a theme directory holding
`colors.toml`, `icons.theme`, `backgrounds/`, and optionally `shell.toml` and a
`light.mode` marker (basecamp/omarchy `c05d9019`, `docs/theming.md`). A community
Omarchy theme works unchanged. `#000000` is not a rule: it is the `background` of
Tom's `noir` theme, and Omarchy's own `vantablack` theme is another pure-black one.
A light theme (`mode = "light"`) must work.

Example themes live in [`../../examples/themes/`](../../examples/themes/): `noir`
(the default), `claude-dark` and `claude-light`, converted from Tom's
`home/themes/*.nix` with no loss (the private scoping note `themes/nix-palette-to-omarchy.py`; its
asserts pass for all three), and `all-black`, the Chrome Web Store theme Tom wears
expressed as a theme directory (see below). Each also carries `gtk.theme`, an extra file that
names the GTK theme (MacTahoe on Tom's seats), and a few extra keys for Tom's
other consumers. Extra keys are valid Omarchy input that no Omarchy consumer
reads; views-shell keeps them and never pins them.

The binding from Omarchy keys to colour ids is
[`../theme-map.json`](../theme-map.json). It holds no colour values.

## How it is applied

Three layers, all through `ui::ColorProvider`. No component code changes for
colour.

1. **The seed.** Every widget's `ColorProviderKey` gets `color_mode` from the
   resolved `mode`, `user_color` from the theme's background (neutralised so a
   near-black seed stays neutral), `user_color_source = kAccent`, and the default
   tonal-spot variant. Every stock role views-shell does not pin is then the same tonal
   rendition Chrome draws its own frame with, from the same seed.
2. **Exact pins.** One `ColorMixer` appended last with
   `ColorProviderManager::AppendColorProviderInitializer` sets the `kColorSys*`
   ids listed in `theme-map.json` to the resolved Omarchy values: `background`
   to the base, surface and header roles, `lighter_background` to the elevated
   surfaces, `accent` to primary and the focus ring, and so on. For `noir` this
   reproduces the old all-black ground exactly.
3. **Per-surface sections.** If the theme has a `shell.toml`, its sections
   (`[bar]`, `[popups]`, `[tooltip]`, `[notifications]`, `[launcher]`, `[menu]`,
   `[polkit]`, `[controls]`, `[font]`, `[spacing]`) override layer 2 for the
   matching views-shell surface. Gradients collapse to their first stop.

`colors.toml` is resolved with a port of Omarchy's own cascade
(`bin/omarchy-theme-color`, MIT, ported with its notice), so a derived key means
the same thing in views-shell as in Omarchy. The reader ships with byte fixtures: this
palette in, these mixer rows out.

**Live switching.** `views-shell theme apply` re-reads the active theme, rebuilds the two
mixers, calls `ColorProviderManager::ResetColorProviderCache()` and notifies the
`ui::NativeTheme` observers, so every view gets `OnThemeChanged()`. No surface is
rebuilt. That mirrors Omarchy's `shell applyTheme` IPC.

**No blur.** No views-shell panel asks for blur. Panels take the theme's background.

## All Black

Tom's Chrome wears the Web Store theme **All Black**,
id `mkplpffahhkjfocfbfapcemhhkgmljpn`. The shell has to be consistent with it, so
[`all-black/`](../../examples/themes/all-black/) is that theme as an Omarchy theme
directory: its frame, toolbar, omnibox and new-tab backgrounds are
`background = "#000000"`, its tab, bookmark and omnibox text is
`foreground = "#ffffff"`, and its button tint toward white is `accent = "#ffffff"`,
whose derived `on_accent` is black. What All Black does not define — the neutral
greys and the ANSI hues — is marked `derived` in that file, and
`tools/validate.py` fails the theme if it carries a key
[`../theme-map.json`](../theme-map.json) does not account for.

How the theme and the stock `ui/color` mixers combine is **order, not merging**:

1. `ui::ColorProvider` runs the stock mixers first: `ui::NativeTheme`'s dark
   mixer, then the tonal mixers that render every `kColorSys*` role from the
   `ColorProviderKey` seed (layer 1 above). Nothing stock is removed or edited.
2. views-shell appends exactly one mixer **last**, with
   `ColorProviderManager::AppendColorProviderInitializer`. The last mixer to set
   an id wins, so All Black's pins (layer 2) overwrite the stock renditions of
   the ids `theme-map.json` names, and every id it does not name keeps the stock
   value the black seed produced.
3. `shell.toml` sections, when the theme ships one, are applied in that same
   appended mixer and win over the pins for their own surface.

So "consistent with All Black" is precise: the pinned ids are literally All
Black's values, and the rest of the kit is Chrome's own tonal dark neutral from
the same `#000000` seed — the same relationship Chrome's frame has to the theme
it wears. The id is what keeps the two halves in step: it is `theme.chromeThemeId`
in the user configuration ([`../../schemas/config.schema.json`](../../schemas/config.schema.json)),
so one config names both — views-shell wears the `all-black` directory, Chrome
wears the Web Store theme of that id (installed by Tom; no extension can apply a
theme for him).

## How Chrome follows

Chrome is not themed by an extension. It follows the same seed through the
`BrowserThemeColor` managed policy, which is what Omarchy writes, and what let it
archive its Chromium micro-fork (`omacom/omarchy-chromium` `05530dfd`: "until
upstream improved the policy-based refreshes"). Upstream's
`--refresh-platform-policy` (Chromium CL 6900896) and the accent fix (CL 7157046)
make a running Chrome pick up a changed policy at once. Tom's Chrome 152 has both.

- The colour is a **seed**, not paint: `noir`'s Chrome frame is Chrome's own dark
  neutral, not literal `#000000`.
- `BrowserColorScheme`, which Omarchy also writes, is **not** a Chrome policy.
  Light and dark keep following the portal's `color-scheme`.
- Writing `/etc/opt/chrome/policies/managed/` needs root. The recommended writer
  lives in the dotfiles, not here: a system `.path` unit watches a request file the
  views-shell theme service writes (six hex digits), and a root service validates it and
  writes the policy file. views-shell itself never needs root. The choice between that
  and Omarchy's narrow sudoers helper is open decision P19.

## GTK

views-shell never writes GTK CSS (rule R26). `gtk.theme` names the GTK theme and the one
gsettings writer applies it. On Tom's seats that is MacTahoe, which is
non-negotiable; no libadwaita stylesheet is generated.

## History

`black.json`, the first token file, mixed a binding (which ids form the ground)
with a theme (the value `#000000`) and keyed most of it on the `cros.sys` and Ash
colour ids that are no longer ported. It was split: the binding is now
`theme-map.json` on `kColorSys*`, and the value is the `noir` example theme.
