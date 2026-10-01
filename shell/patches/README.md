# patches

Lifted untrimmed from agency-mvp `cd7cef3`. Planned names and order
(`series.proposed`); none of this has been applied to a tree in this repository.

| Lifted file | Becomes | Job | Planned change |
|---|---|---|---|
| `agency-source.patch` | `views-shell-ozone-layer-shell.patch` | `PlatformWindowType::kLayerShell`, init properties, factory case, global registration, DCHECK widening, virtual `OnKeyboardFocusChanged` (activation fix `a1b4fc0`) | trim to layer-shell only; fold in the factory fix |
| `agency-layershell-factory-fix.patch` | folded into the above | fixes the enum placement that made every popup a layer surface | fold |
| `agency-layershell-popup.patch` | `views-shell-ozone-layer-popup.patch` | `xdg_popup` children of layer surfaces | live acceptance pending |
| `ozone-empty-opaque-region.patch` | `views-shell-ozone-empty-opaque-region.patch` | stops translucent windows being declared opaque (the "black box") | none |
| `agency-build-gate.patch` | `views-shell-build-gate.patch` | root `gn_all` reaches `//views-shell`; `BUILDFLAG` plumbing | rename flag |
| `agency-dbus-visibility.patch` | `views-shell-dbus-visibility.patch` | one shared session-bus owner | rename paths |

Applied with plain `git apply`, from a pristine tree, in this order. Never a
three-way merge.
