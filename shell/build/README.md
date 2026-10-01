# build

Lifted from agency-mvp `cd7cef3` (see `../PROVENANCE.md`). These files still say
`agency`; the rename is part of the re-cut.

- `CHROMIUM_VERSION`: the last green tag in this lineage, **150.0.7871.124**.
  views-shell's first hop re-cuts at a current tag, chosen by hand
  ([`../../docs/chromium-hop.md`](../../docs/chromium-hop.md)).
- `args.gn`: the lifted development configuration (component build). Planned
  changes before first use: drop `agency_enable_inbound_server`; replace
  `enable_agency` with `enable_views_shell`; **set `dcheck_always_on = false`
  explicitly**. The lineage set it because the renderer and Blink aborted with
  DCHECKs on at 148 and 149; views-shell has no renderer (rule R24), so the reason is
  gone, but the argument stays explicit until a content-free build shows it can
  be dropped.
- `args.release.gn`: a second configuration, release and non-component, for
  footprint measurement only. The June 30 figures came from a component dev
  build; nothing comparable has been measured since. `views_examples` (no
  `//content`) is built beside `//views_shell:views-shell` in this configuration as the control.
- `wire-agency.sh`, `wire-and-gen.sh`, `verify-compilation.sh`, `LINK-GATE.md`:
  the wire and link gates. Planned: drop the host-overlay weld and the inbound
  server; never fall back to `git apply --3way`; build the Views program with
  `assert_no_deps` on `//chrome`, `//ash`, `//chromeos`, `//content`,
  `//third_party/blink/renderer` and `//v8`.

Hops are run by hand on the build host, within the compile budget Tom sets. There
is no timer and no CI job.

## Measuring footprint

Once a tree builds, measure `//views_shell:views-shell` and `views_examples` from
`args.release.gn` with the June 30 harness (`june30/measure.sh`) or
the private scoping note `noctalia-perf-raw/inner.sh`, under
`~/.local/bin/runtime-test --` in a nested headless scroll, N ≥ 3 samples each:
stripped size, `ldd`, whole-tree PSS from `smaps_rollup`, idle CPU and ctxsw/s.
Set `HOME` and the XDG directories before `dbus-run-session`, and point the system
bus at a dead address, so nothing reaches the live dconf or logind (rule R19).
A GPU-fair run needs `/dev/dri` inside the sandbox, which `runtime-test` does not
expose today (open decision P23).
