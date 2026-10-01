# Open decisions (PROPOSED items)

Anything in this repository marked **PROPOSED** waits on Tom. This page collects
them. Nothing here blocks the foundation. Each item names what the repository does
in the meantime. Closed items keep their numbers.

Tom's reasoned list, with a recommendation for each, is in the private scoping notes.

## Closed
| # | Decision | Ruling |
|---|---|---|
| P1 | A Views file selector served as the portal's file chooser | **Out** (Tom, 2026-10-01). Google did not build one in Views, Tom will not, and it is rarely used. Nautilus serves the chooser through `xdg-desktop-portal-gnome`. Rule R3. |
| P2 | A Views file manager | **Out** (Tom, 2026-10-01), for the same reasons. Nautilus stays. Rule R3. |
| P7 | Whether the old expansion appears anywhere public | **Closed** (Tom, 2026-10-01): the expansion is dead and appears nowhere. See P14. |
| P3 | The copyright holder string on new files | **Closed** (2026-10-01): "The views-shell Authors". |
| P12 | Pinning the extension id with a manifest `key` | **Closed** (2026-10-01): the key is pinned in `extension/manifest.json`; the id is recorded in `identity.json`. The private key is held by Tom outside this repository. |
| P14 | The product name | **Closed** (Tom, 2026-10-01): views-shell. See [`naming.md`](naming.md). |
| P15 | A scroll hook patch queue | **Closed** (Tom, 2026-10-01): stock scroll, no patches, no fork. Hooks are upstream wishes only. |
| P17 | Lua in a fork | **Closed** with P15: there is no fork. |
| P13 | Which Chromium branch to track (every Stable or every Extended milestone) | **Superseded** by the release policy (Tom, 2026-10-01): no releases and no tracking promise before v1; v1 is the Chromium tag of the Chrome release Tom names. See [`chromium-hop.md`](chromium-hop.md) and P24. |

## Open
| # | Decision | What the repository does meanwhile |
|---|---|---|
| P4 | Retired lineage identifiers in lifted code (the old layer-shell build flag and connection accessor, the `agency-*` namespaces, the original copyright-holder lines) | Lifted byte for byte. A rename is planned as its own commit after the lift, so the lift diffs cleanly against provenance. |
| P5 | Producer source of truth | agency-mvp's compiled producers are treated as primary; views-shell drafts and aurade patches are design reference. Producers are now source plugins (P25); their snapshot schemas are new either way. |
| P6 | Lock screen | Not a views-shell surface (rule R4). Whether the seats bind a locker at all is P31. |
| P8 | Slot-file location | `~/.config/scroll/views-shell.d/*.conf` for scroll and sway. The alternative is `$XDG_STATE_HOME/views-shell/<compositor>.d/`, included by absolute path, because `~/.config/scroll` is a symlink into the dotfiles checkout. |
| P9 | The `ui` tree node set | The first set plus `mediaSession` ([`../schemas/ui-tree.schema.json`](../schemas/ui-tree.schema.json)). To reconcile with the 33 component atoms in the theming notes and with upstream's `ui/views/examples/json_view_builder_schema.md` (Views Canvas), and to decide whether views-shell follows, copies or links `JsonViewBuilder`. |
| P10 | The extension's suggested in-Chrome key | `Ctrl+Shift+K`, with `Ctrl+Shift+Y` as the alternate. |
| P11 | Whether the lifted agency-mvp and June-embedder code may become public | In the local repository only. Both sources are Tom's own private repositories. |
| P16 | Send the upstream wishes to dawsers (outward-facing) | Nothing sent. See [`scroll-fork-hooks.md`](scroll-fork-hooks.md). |
| P18 | Theme paths and extra key names | views-shell reads `$XDG_CONFIG_HOME/views-shell/themes/` and `~/.config/omarchy/themes/` (PROPOSED). Tom's extra keys (`brand`, `terminal_color7`, `nvim_catppuccin_flavour`, …) keep bare names. |
| P19 | The Chrome policy writer | Not in this repository. Recommended: a system `.path` unit and a root writer that accepts six hex digits, in the dotfiles. Alternative: Omarchy's narrow sudoers helper. |
| P20 | Adapter order | scroll and sway, niri, then the generic ext-workspace adapter (PROPOSED) before Hyprland. |
| P21 | MacTahoe on scroll without blur | Translucent MacTahoe GTK4 surfaces draw unfrosted. Alternatives: MacTahoe's solid variant, or blur in the fork (H13). |
| P22 | Google Drive `appDataFolder` | Not in v1. `chrome.storage.sync` carries light references only. |
| P23 | A GPU-fair footprint measurement | Not run. Needs `/dev/dri` inside `runtime-test` (a new flag) or a microVM with GPU passthrough. |
| P24 | How views-shell moves after v1 | Left for v1. |
| P25 | Producers as built-in source plugins | Done as a proposal in the schema and `examples/network-source/`. The alternative keeps producers inside the core framework. |
| P26 | The two ported quick-settings faces (FeatureTile, QuickSettingsSlider) | Ported (ledger A032, A033) for the look. The alternative is a pure-stock tile and `views::Slider`, which takes the ledger to three rows. |
| P27 | `views-shell.*` ids open to declarative plugins | Allowed by the schema. The alternative is a separate first-party publisher prefix for T1 plugins. |
| P28 | Bar pills fed by source plugins through a bar slot, like the tiles | Planned in `shell/PROVENANCE.md`; no schema yet. |
| P29 | Quick-settings shape: one host with a grid (as now), or independent per-domain bubbles anchored to bar indicators (Agency D12 FR-046) | One host. Every item is a plugin either way. |
| P30 | Per-plugin memory ceiling and crash budget for T2 processes (after noctalia's Luau limits) | Not in the manifest yet. |
| P31 | Whether the seats bind a screen locker | None bound. swayidle turns the monitors off. |
