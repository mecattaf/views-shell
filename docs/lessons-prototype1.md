# Lessons of the first prototype

The first prototype (2026-06-29) was a lift of roughly 23k lines of Ash and
ChromeOS UI code into an additive tree, meant to become a desktop shell for a
Linux compositor. It was never compiled. This note records what it taught, so
that views-shell does not pay for the same lessons twice. It cites the prototype
by shape, not by path: the prototype lives in a private archive, and this
repository keeps no copy of it (open decision P11).

## 1. A draft grounded file:line is not evidence until it is compiled

Every file in the prototype cited its Chromium source by file and line, and
many cited the exact API they called. That grounding looked like verification,
but it was not. An API that exists at `foo.h:120` can still be unreachable from
the target that calls it (visibility, `assert(is_chromeos)`, a missing dep), can
take a different argument at the tag that is actually checked out, or can be
private to a component the program does not link. None of that shows up until
`gn gen` and `autoninja` run against a real tree.

- A lifted or drafted file counts as **draft** until the bench has compiled it at
  the tracked tag (154.0.8037.92 in this chapter).
- `docs/upgrade/VERIFIED-APIS.md` is useful as a list of the APIs that were
  used. It is not proof that a file builds.
- Some code did compile in an earlier prototype (agency-mvp at 150, see
  `shell/PROVENANCE.md`). That shows the code was buildable at 150. It does not
  show the code builds at 154, which stays an open question until the bench
  answers it.

## 2. `target_os=chromeos` forces `use_glib=false`

The prototype built with `target_os="chromeos"` so that the Ash and ChromeOS
targets it depended on would resolve. That choice has a cost that is easy to
miss. A ChromeOS build sets `use_glib=false`, so no GLib main loop runs in the
browser process. Any client library that expects one is stranded. The
polkit agent was the clearest case: libpolkit-agent and its GObject signals
dispatch through a GMainContext.

The prototype's workaround was a glib-to-epoll bridge: a hand-iterated
`GMainContext` whose fds were polled from the Chromium message pump. It
worked on paper, but it was code that existed only because of the build target,
and it had to be kept correct against two event loops.

The lesson: choose the target OS first, and only then choose the code to lift.
views-shell is `is_linux`, where `use_glib=true` is the default. GLib clients
then work as they do in desktop Chrome, and the bridge never needs writing.

## 3. The shared utilities every surface re-rolled

Each surface in the prototype was lifted or drafted by itself, and each one
ended up re-implementing the same four pieces:

| Re-rolled piece | What each surface did | What views-shell does once |
|---|---|---|
| A panel base | its own layer-surface widget setup, anchors, exclusive zone, show/hide and focus handling | one `SurfaceSpec` registry and one host, consumed by every surface (rule R1) |
| A menu-model builder | its own `ui::SimpleMenuModel` assembly and `MenuRunner` plumbing | one builder over `ui::SimpleMenuModel` and the stock menu stack (`style/INVENTORY.md`) |
| A command runner | its own `base::LaunchProcess` wrapper with ad-hoc argv, environment and exit handling | one runner, and compositor actions go through the adapter, never through a subprocess (rule R6) |
| A session `dbus::Bus` | a private `dbus::Bus` connection per producer | one session bus and one system bus, each with a single owner (rule R8) |

The lesson: write a shared utility the second time a surface needs it, before
the third surface copies it. A utility added after five surfaces have their own
copies costs five migrations.

## 4. Normalise directory spelling before cross-references exist

The prototype's tree grew more than one spelling for the same directory (for
example singular beside plural, or the same word with and without an
underscore). Each spelling soon picked up
include paths, GN labels, provenance tables and doc links. Renaming them later
meant touching every one of those references, and a rename that misses one
breaks the build or leaves a broken link.

The lesson: fix the layout and its spelling while the tree is still empty, then
add files. In views-shell the layout is in `shell/README.md`, and the rename of
retired lineage identifiers in lifted code is planned as one separate commit
(open decision P4). That commit lands before more cross-references pile up.

## 5. Readiness tiers, and which surfaces were lift-dominant

The prototype sorted its surfaces into readiness tiers. The useful version
of that scale, kept here, is:

| Tier | Meaning |
|---|---|
| compiled | built at a named tag in a real tree; the only tier that is evidence |
| lifted | upstream code copied with its header, not yet compiled in this tree |
| drafted | new code written against cited APIs, not yet compiled |
| pattern | upstream code read for shape, rewritten on stock Views |

In the prototype no surface reached **compiled**. Most code sat in **lifted**,
and the surfaces where the lift dominated were the ones Ash draws as thin skins
over stock controls: the style kit (colour ids, typography, shadows, buttons,
switches, combobox), the quick-settings tiles, sliders and detailed views, and
the system dialogs. Those are the rows that `ASH-PORT-LEDGER.md` now records as
**Dropped**, each with the stock replacement that makes the lift unnecessary
(35 of the 40 rows first planned). In a lift-dominant surface most of the
lines were Ash glue around a control `ui/views` already provides.

The surfaces that needed real new code (the workspace rail, the compositor
adapter, the producers over system daemons) are the ones a lift could never
supply, because Ash's versions talk to its own window manager and session.

The lesson: a high line count in **lifted** looks like progress but measures
coupling to Ash. The number worth tracking is how many surfaces are **compiled**.

## 6. What views-shell does differently

- **`is_linux`, not `is_chromeos`.** views-shell is an `is_linux` program. It keeps
  `use_glib` at its default, and so needs no event-loop bridge (section 2).
- **Views first.** Stock `ui/views`, `ui/color`, `ui/base` and
  `ui/message_center` are the donor. An Ash file is copied only when it has no
  wm or session reference, has no stock twin within one subclass, and a surface
  needs it now (rule R5, `ASH-PORT-LEDGER.md`).
- **No Ash link.** `//ash`, `//chromeos` and `//chrome` are forbidden by
  `assert_no_deps` on the executable, along with `//content`, the Blink renderer
  and `//v8` (rules R1, R24). A lifted file cannot quietly pull Ash back in.
- **Compile first.** The bench (`tools/bench/`) builds `views_examples` and then
  `//views_shell:views_shell` at the pinned tag before any surface is extended.
  Each claim in `SPEC.md` is proven by a command that runs on the bench. Nothing
  counts as done because its citations look right (section 1).
- **Shared utilities first.** The panel base, menu-model builder, command runner
  and bus owners are written once, before the second surface needs them
  (section 3).
- **Layout fixed early.** Directory spelling is settled before files reference
  each other (section 4).
