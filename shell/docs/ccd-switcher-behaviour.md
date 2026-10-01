# The 2026-06-27 workspace switcher: behaviour spec

This is the only switcher in the project's history that switched workspaces on a
live compositor. It was web content (about 180 lines of JavaScript) on a layer
surface, hosted by a Content-API embedder on Chromium 149.0.7827.102, under niri.
Its code is **not** lifted. Its behaviour is transcribed here so the views-shell rail can
be tested against it. Source: `surfaces/ccd/app.js` and `surfaces/lib/services.js`
lines 637-700 of the June embedder (see `../PROVENANCE.md`), and the 2026-06-27
report, which marks it live and checked over CDP. Click-to-pixel time was never
measured.

## Surface

| Property | Value | views-shell equivalent |
|---|---|---|
| Layer | top | `SurfaceSpec.layer = kTop` |
| Anchors | top, bottom, left | same |
| Width | primary output width / 8 (expanded), output width / 20 (collapsed), in DIP | collapsed 56 DIP and expanded 240 DIP, after Chrome's vertical tabs (configurable) |
| Exclusive zone | equal to the width, so niri re-tiles on collapse | the collapsed rail keeps its zone; the expanded state is a separate overlay surface with **no** zone, so hovering never re-tiles |
| Keyboard | on-demand | none for the rail; on-demand for the flyout; exclusive only while renaming |
| Namespace | the old lineage's `-ccd` namespace | `views-shell-rail`, `views-shell-rail-flyout` |
| Transparency | translucent window, transparent page background, plus the empty-opaque-region patch | the theme's background, opaque (no blur); the opaque-region patch is still carried for menus and rounded corners |

## State

1. At start, fetch workspaces, windows and outputs in parallel.
2. Follow the event stream, one JSON object per line. On `WorkspacesChanged`,
   refetch workspaces (debounced).
3. Keep the focused workspace as a live scalar from the event stream, not from each
   workspace's `is_focused` flag, which lags across multi-output focus changes.

In views-shell the adapter does this in process, on an IO thread, and the `WmModel` holds
the focused workspace as its own field.

## Rendering

1. Group workspaces by output. Sort groups by output name, and workspaces within a
   group by index.
2. Show the output name only when there is more than one output.
3. Each row: an index badge, the label (the workspace name, or "Workspace N"), and
   the window count (blank when zero).
4. Mark the focused workspace, and separately the active workspace of each output.

## Interaction

| Input | Action |
|---|---|
| Click a row | Focus that workspace (`focus-workspace <id>`). |
| The collapse button | Toggle collapsed and expanded, resizing the surface and its zone. |
| Escape | Toggle collapse (never hide; full hide belongs to the bar's toggle, so the two cannot desynchronise). |
| Ctrl+wheel, Ctrl +/-/0 | Ignored: a shell surface is not a document and must not zoom. |

## Compositor settings that mattered

- `animations { window-movement { off } horizontal-view-movement { off } }` on niri
  removed the re-tile animation and was the biggest single improvement in toggle
  feel.

## Acceptance tests derived from it

Under `runtime-test`, in a nested headless compositor, with a scripted snapshot of
three workspaces on one output and one on a second:

1. Groups and order match rules 1 and 2 of "Rendering".
2. The focused row follows a focus change made by the compositor, not by views-shell.
3. **A click on a row focuses that workspace, and the row's focused state changes
   only after the compositor's event arrives** (the observed echo).
4. Collapse changes the surface width and its exclusive zone together, and the
   compositor's usable area changes by the same amount.
5. Escape collapses and expands, and never unmaps the rail.
6. Ctrl+wheel does not scale the surface.
7. The compositor's window list contains no views-shell window at any point.
