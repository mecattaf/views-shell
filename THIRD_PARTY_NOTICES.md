# Third-party notices

views-shell keeps every original licence header on the files it copies. This file
lists the sources. Each copied file is also recorded either in
[`ASH-PORT-LEDGER.md`](ASH-PORT-LEDGER.md) (for files from `//ash`, `//ui` and
other Chromium directories) or in [`shell/PROVENANCE.md`](shell/PROVENANCE.md)
(for code lifted from earlier prototypes).

| Source | Licence | How views-shell uses it |
|---|---|---|
| Chromium (`//ui`, `//components`, five files from `//ash` and `//chromeos`, `//chrome` patterns), "The Chromium Authors" | BSD-3-Clause | Ported files keep their header. Unbranded resources only. |
| Earlier layer-shell prototypes by the same author ("The Agency Authors" and an earlier lineage holder; see `shell/PROVENANCE.md`) | BSD-3-Clause | Lifted byte for byte, with provenance lines. |
| aurade (Cam396) | BSD-3-Clause | Read as adapter patterns. Copied only with notices, if at all. |
| scroll (dawsers), sway | MIT | Read for IPC framing. Not copied. |
| niri | GPL-3.0 | Read for IPC shapes only. Never copied. |
| Hyprland | BSD-3-Clause | Read for socket protocol. Not copied. |
| Omarchy (basecamp/omarchy) | MIT | The theme input format is Omarchy's, exactly. The `colors.toml` resolver is a planned port of `bin/omarchy-theme-color`, copied with its MIT notice. The plugin model is inspiration only. |
| noctalia (noctalia-dev/noctalia) | MIT | The adapter-per-compositor lineage and its generic `ext-workspace` backend, read as design reference. Not copied. |
| VS Code (manifest reference) | MIT | Contribution-manifest inspiration only. Not copied. |

views-shell uses code from the Chromium project. It is not affiliated with Google. Do
not write "by the Chromium team". The BSD-3 endorsement clause forbids it.
