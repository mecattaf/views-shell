# Upgrade precedent (lifted)

Lifted byte for byte from agency-mvp `cd7cef3` (see `../../shell/PROVENANCE.md`).
They are the precedent for [`../chromium-hop.md`](../chromium-hop.md), not views-shell
process documents.

- `CHROMIUM-UPGRADE.md`: agency's upgrade protocol. views-shell keeps its discipline
  (pristine tree, strict apply, the drift taxonomy, the `difflib` re-cut, literal
  success tokens) and drops its `--3way` fallback.
- `UPGRADE-LEDGER-150.md`: the 149 → 150 hop that ended green.
- `VERIFIED-APIS.md`: Views and Ozone APIs checked against the 150 headers. Re-verify
  at the re-cut.

Their links to `UPGRADE-LEDGER-149.md` and to `evidence/*.png` point at files that
were not lifted; those stay in agency-mvp.

These three files are a historical record. They name `agency-*.patch`,
`wire-agency.sh`, `bar_view.cc`, `clock_controller.cc` and the 150 tag, all of
which were deleted or re-cut in chapters 1 and 2 (`../../shell/PROVENANCE.md`);
the live series is `../../shell/patches/series` and the live procedure is
`../../tools/bench/`. Nothing here is edited to match the tree.
