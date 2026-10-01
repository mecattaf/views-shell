# Naming

## Plugins and extensions

These two words are never interchanged.

| Word | Means | Never means |
|---|---|---|
| **plugin** | A views-shell shell module, described by a `views-shell-plugin.json` manifest, in tier T0, T1, T2 or T3 | a Chrome extension |
| **extension** | A Google Chrome extension. In this repository that is the views-shell Chrome extension in [`extension/`](../extension/) | a views-shell shell module |

Consequences:
- The manifest is `views-shell-plugin.json`, never `extension.json`.
- The CLI group is `views-shell plugin …`.
- Code, schema and docs say `plugin` for shell modules. A field or class named
  `extension*` refers to Chrome.
- The Chrome-facing contribution points in a plugin manifest are grouped under
  `chrome` (for example `settingsPages`), because they are rendered by the
  extension. They are still part of a plugin.

Older scoping notes used "extension" for shell modules. Read those as "plugin".

## The product

- **The name is views-shell** (Tom, 2026-10-01: "views-shell it is"). The repository,
  binary, CLI and paths say `views-shell`; C++ and GN say `views_shell`; prose says
  views-shell. It is a plain description, Chromium Views on layer-shell, and it
  carries no Google mark: Google's brand guidance forbids its marks inside a
  product name ("Don't put our name in your name"). Chromium, Chrome and ChromeOS
  are listed marks; Aura, Ash, Views and Exo are not.
- **There is no expansion.** The name is not an acronym. An earlier working name
  had an expansion containing a Google mark; that expansion is dead (Tom,
  2026-10-01). The product is not a "unified browser and shell": the browser is
  the stock Google Chrome you already run.
- **The rename happened on 2026-10-01**, after the extension `key` was pinned
  (`identity.json`), so the extension id and its `chrome.storage.sync` data are
  independent of any directory path.
- The C++ namespace is `views_shell`, never `views`, `ash`, `aura` or `exo`.
- "Ash" and "Exo" are used descriptively only.
- Retired lineage names are not used in new text. Lifted files keep their
  original headers and identifiers until a separate rename commit (PROPOSED, see
  [`open-decisions.md`](open-decisions.md)).

## Identifiers

| Thing | Form | Example |
|---|---|---|
| Plugin id | `publisher.name`, permanent, `views-shell.*` reserved | `views-shell.workspace-rail`, `example.power-menu` |
| Command id | `<plugin-id>/<command>` when fully qualified, `<command>` inside its manifest | `views-shell.workspace-rail/focus` |
| Capability | dotted, family first | `workspaces.focus`, `scroll.lua` |
| Layer-shell namespace | `views-shell-<surface>` | `views-shell-bar`, `views-shell-rail`, `views-shell-rail-flyout` |
| C++ namespace | `views_shell` | `views_shell::WmModel` |
| GN build flag | `enable_views_shell`, `BUILDFLAG(ENABLE_VIEWS_SHELL)` | |
| Native messaging host | `dev.mecattaf.views_shell` | manifest `$XDG_CONFIG_HOME/google-chrome/NativeMessagingHosts/dev.mecattaf.views_shell.json`, `allowed_origins` = the pinned extension id in `identity.json` |
| Slot file | one generated file per plugin per slot; paths per compositor in [`../schemas/keybinding-rendering.md`](../schemas/keybinding-rendering.md) | `~/.config/scroll/views-shell.d/example.power-menu.conf` |

## Phrases

Say "uses code from the Chromium project" and "not affiliated with Google". Do not
say that Google killed ChromeOS, "ChromeOS for Linux", "Chrome Shell", or anything
else that puts a Google mark in a product name. Do not say "by the Chromium team".
