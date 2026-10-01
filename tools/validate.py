#!/usr/bin/env python3
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Repository check: every JSON file parses, the schemas are valid Draft 2020-12,
every example manifest validates against views-shell-plugin.schema.json, every ui/*.json
against ui-tree.schema.json, every examples/config/*.json against
config.schema.json (and against the 8,192-byte chrome.storage.sync per-item quota), every file a manifest names exists, every
quick-settings entry that opens a page names a declared page, every qualified
command a manifest handler or ui tree calls belongs to the plugin or is covered
by a call: permission, every examples/themes/*/ Omarchy theme directory holds a
parseable colors.toml whose keys are all accounted for by style/theme-map.json,
and every fixture under tools/fixtures/invalid/ is rejected.

Needs the python `jsonschema` package (>= 4.18). On NixOS:
  nix shell nixpkgs#python3Packages.jsonschema -c python3 tools/validate.py
Prints the literal token VALIDATE-OK on success (not with --defer-ok, which
tools/validate.sh passes so it can print the token after the plugin-conformance
runs). tools/plugin-conformance.py imports manifest_problems() and validator()
from here, so a manifest is checked the same way by both.
"""
import filecmp
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import tomllib

from jsonschema import Draft202012Validator

ROOT = pathlib.Path(__file__).resolve().parent.parent
SKIP = {".git", "node_modules"}
QUALIFIED = re.compile(r"^([a-z0-9][a-z0-9-]*(?:\.[a-z0-9][a-z0-9-]*)+)/([a-z][a-z0-9-]*)$")
HEX6 = re.compile(r"^#[0-9a-f]{6}$")
WORD = re.compile(r"^[a-z0-9_-]+$")


def commands_in(node, out):
    """Every string value under a "command" or "call" key, anywhere in node."""
    if isinstance(node, dict):
        for k, v in node.items():
            if k in ("command", "call") and isinstance(v, str):
                out.append(v)
            else:
                commands_in(v, out)
    elif isinstance(node, list):
        for v in node:
            commands_in(v, out)
    return out


def covered(plugin_id, perms, command):
    m = QUALIFIED.match(command)
    if not m or m.group(1) == plugin_id:
        return True
    return f"call:{m.group(1)}/*" in perms or f"call:{command}" in perms


def load(path):
    return json.loads(path.read_text())


def validator(name):
    """A Draft 2020-12 validator for schemas/<name>.schema.json (the schema itself
    checked first)."""
    schema = load(ROOT / "schemas" / f"{name}.schema.json")
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def manifest_problems(manifest, plugin_v):
    """Checks one views-shell-plugin.json. Returns (schema_errors, other_errors),
    each a list of "<path>: <message>" strings. tools/plugin-conformance.py runs
    the same function before it spawns a process plugin."""
    rel = manifest.relative_to(ROOT) if manifest.is_relative_to(ROOT) else manifest
    data = load(manifest)
    schema_errs = [f"{rel}: {'/'.join(map(str, e.path))}: {e.message}"
                   for e in sorted(plugin_v.iter_errors(data), key=lambda e: list(e.path))]
    failures = []
    base = manifest.parent
    named = []
    rt = data.get("runtime", {})
    if "exec" in rt:
        named.append(rt["exec"])
    c = data.get("contributes", {})
    named += [s["ui"] for s in c.get("surfaces", []) if "ui" in s]
    named += [q["ui"] for q in c.get("quickSettings", []) if "ui" in q]
    named += [x["schema"] for x in c.get("sources", []) if "schema" in x]
    named += [x["path"] for x in data.get("chrome", {}).get("settingsPages", [])]
    named += c.get("compositor", {}).get("scroll", {}).get("lua", [])
    named += c.get("compositor", {}).get("scroll", {}).get("config", [])
    for n in named:
        if not (base / n).is_file():
            failures.append(f"{rel}: names missing file {n}")
    # Every command referenced locally must be declared.
    ids = {cmd["id"] for cmd in c.get("commands", [])}
    refs = [k["command"] for k in c.get("keybindings", [])]
    refs += [m["command"] for m in c.get("menus", [])]
    refs += [l["command"] for l in c.get("launcher", [])]
    refs += [v["command"] for v in c.get("cli", {}).get("verbs", [])]
    for r in refs:
        if "/" not in r and r not in ids:
            failures.append(f"{rel}: references undeclared command {r}")
    # A quick-settings entry that opens a page must name a declared page entry.
    qs = c.get("quickSettings", [])
    pages = {q["id"] for q in qs if q.get("slot") == "page"}
    for q in qs:
        if "page" in q and q["page"] not in pages:
            failures.append(f"{rel}: entry {q['id']} opens undeclared page {q['page']}")
    # Cross-plugin calls need a call: permission (handlers and ui trees alike).
    perms = set(data.get("permissions", []))
    calls = commands_in([cmd.get("handler", {}) for cmd in c.get("commands", [])], [])
    for n in named:
        if n.endswith(".json") and (base / n).is_file():
            calls += commands_in(load(base / n), [])
    for call in calls:
        if not covered(data.get("id"), perms, call):
            failures.append(f"{rel}: calls {call} without a call: permission")
    return schema_errs, failures


def main(argv):
    # --defer-ok: on success print no final token; tools/validate.sh prints
    # VALIDATE-OK itself after the plugin-conformance runs.
    defer_ok = "--defer-ok" in argv[1:]
    failures = []
    files = [p for p in ROOT.rglob("*.json") if not SKIP & set(p.parts)]
    for p in files:
        try:
            load(p)
        except json.JSONDecodeError as e:
            failures.append(f"{p.relative_to(ROOT)}: not JSON: {e}")
    print(f"parsed {len(files)} JSON files")

    plugin_v, ui_v, config_v = (validator(n) for n in ("views-shell-plugin", "ui-tree", "config"))
    for name in ("views-shell-plugin", "ui-tree", "config"):
        print(f"schema ok: {name}")

    for manifest in sorted(ROOT.glob("examples/*/views-shell-plugin.json")):
        rel = manifest.relative_to(ROOT)
        schema_errs, other = manifest_problems(manifest, plugin_v)
        failures += schema_errs + other
        print(f"{'FAIL' if schema_errs else 'ok  '} manifest {rel}")

    # User configuration examples: schema-valid and sized for one
    # chrome.storage.sync item (QUOTA_BYTES_PER_ITEM = 8,192 bytes).
    for cfg in sorted(ROOT.glob("examples/config/*.json")):
        rel = cfg.relative_to(ROOT)
        data = load(cfg)
        errs = sorted(config_v.iter_errors(data), key=lambda e: list(e.path))
        for e in errs:
            failures.append(f"{rel}: {'/'.join(map(str, e.path))}: {e.message[:300]}")
        size = len(json.dumps(data, separators=(",", ":")).encode())
        if size > 8192:
            failures.append(f"{rel}: {size} bytes exceeds the 8,192-byte chrome.storage.sync per-item quota")
            errs = errs or [True]
        print(f"{'FAIL' if errs else 'ok  '} config   {rel} ({size} of 8192 sync item bytes)")

    # The sync probe embeds a copy of examples/config/default.json (an extension
    # page cannot reach outside the extension directory); it must not drift.
    probe = (ROOT / "extension/sync-probe.js").read_text()
    m = re.search(r"// BEGIN examples/config/default\.json.*?const DEFAULT_CONFIG = (\{.*?\n\});\n// END",
                  probe, re.DOTALL)
    if not m:
        failures.append("extension/sync-probe.js: embedded default config block not found")
        print("FAIL config   extension/sync-probe.js (embedded copy)")
    else:
        embedded = json.loads(m.group(1))
        ok = embedded == load(ROOT / "examples/config/default.json")
        if not ok:
            failures.append("extension/sync-probe.js: embedded default config differs from examples/config/default.json")
        print(f"{'FAIL' if not ok else 'ok  '} config   extension/sync-probe.js (embedded copy)")

    for tree in sorted(ROOT.glob("examples/*/ui/*.json")):
        rel = tree.relative_to(ROOT)
        errs = list(ui_v.iter_errors(load(tree)))
        for e in errs:
            failures.append(f"{rel}: {'/'.join(map(str, e.path))}: {e.message[:300]}")
        print(f"{'FAIL' if errs else 'ok  '} ui tree  {rel}")

    # Example themes: each is an Omarchy theme directory whose colors.toml parses
    # and whose every key is accounted for by style/theme-map.json (pinned, seeded,
    # derived by the cascade, or deliberately left unpinned).
    theme_map = load(ROOT / "style/theme-map.json")
    coverage = theme_map["key_coverage"]
    colour_keys = {k.split(" ")[0] for k in theme_map["layer2_pins"] if k != "note"}
    for group in ("layer1_seed", "derived_by_the_cascade", "unmapped_keys",
                  "extra_colour_keys"):
        colour_keys |= set(coverage[group])
    colour_keys.discard("mode")
    word_keys = set(coverage["extra_word_keys"])
    for colors in sorted(ROOT.glob("examples/themes/*/colors.toml")):
        rel = colors.parent.relative_to(ROOT)
        errs = []
        with colors.open("rb") as fh:
            data = tomllib.load(fh)
        if data.get("mode") not in ("dark", "light"):
            errs.append(f"{rel}/colors.toml: mode is neither dark nor light")
        for k, v in data.items():
            if k == "mode":
                continue
            if k in colour_keys:
                if not (isinstance(v, str) and HEX6.match(v)):
                    errs.append(f"{rel}/colors.toml: {k} is not a #rrggbb colour")
            elif k in word_keys:
                if not (isinstance(v, str) and WORD.match(v)):
                    errs.append(f"{rel}/colors.toml: {k} is not a lowercase name")
            else:
                errs.append(f"{rel}/colors.toml: key {k} is not accounted for "
                            "by style/theme-map.json key_coverage")
        for f in ("gtk.theme", "icons.theme"):
            p = colors.parent / f
            if not p.is_file() or not p.read_text().strip():
                errs.append(f"{rel}: missing or empty {f}")
        failures += errs
        print(f"{'FAIL' if errs else 'ok  '} themes   {rel} "
              f"({len(data)} colors.toml keys covered by style/theme-map.json)")

    # Negative fixtures: each must be rejected.
    for bad in sorted(ROOT.glob("tools/fixtures/invalid/*.json")):
        rel = bad.relative_to(ROOT)
        v = ui_v if bad.name.endswith(".ui.json") else plugin_v
        if v.is_valid(load(bad)):
            failures.append(f"{rel}: invalid fixture was accepted")
            print(f"FAIL reject   {rel}")
        else:
            print(f"ok   reject   {rel}")

    # Slot rendering goldens: views-shell plugin render must reproduce cli/testdata.
    plugins = sorted(str(p.parent) for p in ROOT.glob("examples/*/views-shell-plugin.json"))
    for comp in sorted(d.name for d in (ROOT / "cli/testdata").iterdir() if d.is_dir()):
        with tempfile.TemporaryDirectory() as tmp:
            cmd = [sys.executable, str(ROOT / "cli/views-shell"), "plugin", "render",
                   "--compositor", comp, "--out", tmp]
            for p in plugins:
                cmd += ["--plugin", p]
            subprocess.run(cmd, check=True, capture_output=True)
            golden = ROOT / "cli/testdata" / comp
            cmp = filecmp.dircmp(golden, tmp)
            bad = cmp.left_only + cmp.right_only + filecmp.cmpfiles(
                golden, tmp, cmp.common_files, shallow=False)[1]
            for b in bad:
                failures.append(f"cli/testdata/{comp}/{b}: render differs from golden")
            print(f"{'FAIL' if bad else 'ok  '} render   {comp}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        print("VALIDATE-FAILED")
        return 1
    if not defer_ok:
        print("VALIDATE-OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
