#!/usr/bin/env python3
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The reference registry view of a views-shell plugin manifest.

    tools/plugin-registry.py <plugin-dir | views-shell-plugin.json>
    tools/plugin-registry.py <plugin-dir> --check tools/fixtures/registry/<id>.json
    tools/plugin-registry.py <plugin-dir> --write   (rewrites that fixture)
    tools/plugin-registry.py --check-all | --write-all

Prints the canonical registry view as JSON with sorted keys, two-space indent
and a final newline. This is the contract the C++ plugin host (w3b) reproduces
byte for byte: what views-shell registers from a manifest without running any
plugin code. The shape and the derivation rules are this docstring; the
committed views are tools/fixtures/registry/<id>.json.

A manifest that tools/validate.py rejects is never registered: the tool prints
`REJECT <file> <reason>` on stdout and exits 1. The reason is the first schema
error (sorted by instance path) or else the first cross-file problem that
validate.py's manifest_problems() reports. The validator is validate.py's own,
imported, never copied.

--check exits 0 when the view equals the fixture byte for byte and 1 with a
unified diff on stderr otherwise. --check-all / --write-all walk every
examples/*/views-shell-plugin.json and every tools/fixtures/invalid/*.json
manifest (the latter must REJECT) and print "registry ok (N)" on success.

Registry view, field by field (arrays keep manifest order unless noted):
  id           the manifest id.
  tier         runtime.mode: builtin (T0), declarative (T1) or process (T2).
  activation   sorted, unique activation events inferred from contributions:
                 onStartup                 a surface of kind bar, bar-widget or
                                           service (drawn or run from start-up)
                 onSurface:<surface>       a surface of kind panel, overlay, menu
                 onPicker:<surface>        a surface of kind picker-provider
                 onCommand:<plugin/cmd>    every declared command
                 onKeybinding:<key>        every keybinding with a resolved key
                 onSource:<plugin/source>  every source the plugin publishes
                 onEvent:<event>           every contributes.events entry
                 onQuickSettings:<entry>   every quick-settings entry
               menus, launcher and cli entries only name commands, so they add
               nothing beyond onCommand.
  commands     [{id: <plugin/cmd>, title, icon, category, enablement, args,
                 confirm, result, handler, surface, verb}]; args items carry
               {name, type, required} with required defaulting to false;
               confirm defaults to false, result to "none"; handler is the
               manifest's handler object for T1 and null otherwise (the schema
               forbids it elsewhere); absent optional fields are null.
  surfaces     [{id, kind, keyboard, anchor, defaultSection, allowMultiple, ui,
                 prefix, when}]; keyboard defaults to "on-demand",
               defaultSection to "center", allowMultiple to false.
  capabilities {required, optional}: requires.compositor, sorted.
  permissions  sorted.
  dependencies requires.plugins, sorted.
  keybindings  [{command: <plugin/cmd>, key, keyFrom, args, when, release,
                 locked, mode}]; key is the literal key, or the default of the
               configuration property keyFrom names (config.<name>), or null
               when that property has no default; release/locked default false.
  menus        [{location, command: <plugin/cmd>, group, when}].
  launcher     [{command: <plugin/cmd>, title, keywords, icon, desktopEntry}];
               keywords default to [], desktopEntry to false.
  cli          null, or {name, verbs: [{verb, command: <plugin/cmd>, summary,
                 confirm}]} with confirm defaulting to false.
  events       contributes.events as written.
  sources      [{id: <plugin/source>, schema}].
  quickSettings [{id, slot, ui, state, size, page, title, order, when}]; size
               defaults to "primary" for a tile and is null for other slots;
               order defaults to 0.
  configuration {title, properties: {<name>: {type, default, scope, ...}}}:
               every property as written, scope defaulting to "global" and
               default to null when absent. A manifest without configuration
               gives {title: null, properties: {}}.
Every command reference is qualified: a bare name becomes <plugin id>/<name>.
"""
import difflib
import json
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
NIX_PYTHON = ('(builtins.getFlake "nixpkgs").legacyPackages.${builtins.currentSystem}'
              '.python3.withPackages (p: [ p.jsonschema ])')


def ensure_jsonschema():
    """Re-runs this tool under nixpkgs' python3 with jsonschema when the local
    python lacks it, as tools/validate.sh does."""
    try:
        import jsonschema  # noqa: F401
    except ImportError:
        if os.environ.get("VIEWS_SHELL_NO_REEXEC"):
            sys.exit("python3 lacks the jsonschema package")
        os.environ["VIEWS_SHELL_NO_REEXEC"] = "1"
        os.execvp("nix", ["nix", "shell", "--quiet", "--impure", "--expr", NIX_PYTHON,
                          "-c", "python3", *sys.argv])


ensure_jsonschema()
sys.path.insert(0, str(ROOT / "tools"))
from validate import manifest_problems, validator  # noqa: E402


def dumps(obj):
    return json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def qualify(plugin_id, ref):
    return ref if "/" in ref else f"{plugin_id}/{ref}"


def registry_view(m):
    pid = m["id"]
    c = m.get("contributes", {})
    q = lambda ref: qualify(pid, ref)  # noqa: E731
    mode = m["runtime"]["mode"]
    conf = c.get("configuration", {})
    props = conf.get("properties", {})

    commands = []
    for cmd in c.get("commands", []):
        commands.append({
            "id": q(cmd["id"]),
            "title": cmd["title"],
            "icon": cmd.get("icon"),
            "category": cmd.get("category"),
            "enablement": cmd.get("enablement"),
            "args": [{"name": a["name"], "type": a["type"],
                      "required": a.get("required", False)}
                     for a in cmd.get("args", [])],
            "confirm": cmd.get("confirm", False),
            "result": cmd.get("result", "none"),
            "handler": cmd.get("handler") if mode == "declarative" else None,
            "surface": cmd.get("surface"),
            "verb": cmd.get("verb"),
        })

    surfaces = [{
        "id": s["id"],
        "kind": s["kind"],
        "keyboard": s.get("keyboard", "on-demand"),
        "anchor": s.get("anchor"),
        "defaultSection": s.get("defaultSection", "center"),
        "allowMultiple": s.get("allowMultiple", False),
        "ui": s.get("ui"),
        "prefix": s.get("prefix"),
        "when": s.get("when"),
    } for s in c.get("surfaces", [])]

    def resolve_key(kb):
        if "key" in kb:
            return kb["key"]
        name = kb["keyFrom"].split(".", 1)[1]
        return props.get(name, {}).get("default")

    keybindings = [{
        "command": q(kb["command"]),
        "key": resolve_key(kb),
        "keyFrom": kb.get("keyFrom"),
        "args": kb.get("args"),
        "when": kb.get("when"),
        "release": kb.get("release", False),
        "locked": kb.get("locked", False),
        "mode": kb.get("mode"),
    } for kb in c.get("keybindings", [])]

    cli = c.get("cli")
    if cli is not None:
        cli = {"name": cli["name"], "verbs": [{
            "verb": v["verb"], "command": q(v["command"]),
            "summary": v.get("summary"), "confirm": v.get("confirm", False),
        } for v in cli["verbs"]]}

    quick = [{
        "id": e["id"],
        "slot": e["slot"],
        "ui": e.get("ui"),
        "state": e.get("state"),
        "size": e.get("size", "primary") if e["slot"] == "tile" else None,
        "page": e.get("page"),
        "title": e.get("title"),
        "order": e.get("order", 0),
        "when": e.get("when"),
    } for e in c.get("quickSettings", [])]

    sources = [{"id": f"{pid}/{s['id']}", "schema": s.get("schema")}
               for s in c.get("sources", [])]

    activation = set()
    for s in surfaces:
        if s["kind"] in ("bar", "bar-widget", "service"):
            activation.add("onStartup")
        elif s["kind"] == "picker-provider":
            activation.add(f"onPicker:{s['id']}")
        else:
            activation.add(f"onSurface:{s['id']}")
    activation |= {f"onCommand:{x['id']}" for x in commands}
    activation |= {f"onKeybinding:{k['key']}" for k in keybindings if k["key"]}
    activation |= {f"onSource:{s['id']}" for s in sources}
    activation |= {f"onEvent:{e}" for e in c.get("events", [])}
    activation |= {f"onQuickSettings:{e['id']}" for e in quick}

    req = m.get("requires", {})
    comp = req.get("compositor", {})
    return {
        "id": pid,
        "tier": mode,
        "activation": sorted(activation),
        "commands": commands,
        "surfaces": surfaces,
        "capabilities": {"required": sorted(comp.get("required", [])),
                         "optional": sorted(comp.get("optional", []))},
        "permissions": sorted(m.get("permissions", [])),
        "dependencies": sorted(req.get("plugins", [])),
        "keybindings": keybindings,
        "menus": [{"location": x["location"], "command": q(x["command"]),
                   "group": x.get("group"), "when": x.get("when")}
                  for x in c.get("menus", [])],
        "launcher": [{"command": q(x["command"]), "title": x["title"],
                      "keywords": x.get("keywords", []), "icon": x.get("icon"),
                      "desktopEntry": x.get("desktopEntry", False)}
                     for x in c.get("launcher", [])],
        "cli": cli,
        "events": list(c.get("events", [])),
        "sources": sources,
        "quickSettings": quick,
        "configuration": {
            "title": conf.get("title"),
            "properties": {name: {**p, "scope": p.get("scope", "global"),
                                  "default": p.get("default")}
                           for name, p in props.items()},
        },
    }


def manifest_path(arg):
    p = pathlib.Path(arg)
    return p / "views-shell-plugin.json" if p.is_dir() else p


def rel(p):
    p = p.resolve()
    return p.relative_to(ROOT) if p.is_relative_to(ROOT) else p


def evaluate(manifest, plugin_v):
    """(view, None) for an accepted manifest, (None, reason) for a rejected one."""
    try:
        data = json.loads(manifest.read_text())
    except (OSError, json.JSONDecodeError) as e:
        return None, f"not JSON: {e}"
    schema_errs, other = manifest_problems(manifest.resolve(), plugin_v)
    problems = schema_errs + other
    if problems:
        # "<file>: <path>: <message>" -> drop the file prefix.
        reason = problems[0].split(": ", 1)[1]
        return None, "(root)" + reason if reason.startswith(": ") else reason
    return registry_view(data), None


def fixture_for(view):
    return ROOT / "tools/fixtures/registry" / f"{view['id']}.json"


def check(text, fixture):
    want = fixture.read_text() if fixture.is_file() else ""
    if text == want:
        return True
    sys.stderr.writelines(difflib.unified_diff(
        want.splitlines(True), text.splitlines(True),
        str(rel(fixture)), "registry view"))
    return False


def run_all(write):
    plugin_v = validator("views-shell-plugin")
    ok, n = True, 0
    for manifest in sorted(ROOT.glob("examples/*/views-shell-plugin.json")):
        view, reason = evaluate(manifest, plugin_v)
        if view is None:
            print(f"REJECT {rel(manifest)} {reason}", file=sys.stderr)
            ok = False
            continue
        text, fixture = dumps(view), fixture_for(view)
        if write:
            fixture.write_text(text)
        elif not check(text, fixture):
            print(f"FAIL registry {rel(manifest)} differs from {rel(fixture)}",
                  file=sys.stderr)
            ok = False
        n += 1
    rejected = 0
    for bad in sorted(ROOT.glob("tools/fixtures/invalid/*.json")):
        if bad.name.endswith(".ui.json"):
            continue
        view, reason = evaluate(bad, plugin_v)
        if view is not None:
            print(f"FAIL registry {rel(bad)} was registered", file=sys.stderr)
            ok = False
        else:
            print(f"REJECT {rel(bad)} {reason}")
            rejected += 1
    if ok:
        print(f"registry ok ({n})")
        print(f"registry rejects ({rejected})")
    return 0 if ok else 1


def main(argv):
    args = argv[1:]
    if args in (["--check-all"], ["--write-all"]):
        return run_all(args[0] == "--write-all")
    if not args or args[0].startswith("-"):
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    manifest = manifest_path(args[0])
    view, reason = evaluate(manifest, validator("views-shell-plugin"))
    if view is None:
        print(f"REJECT {rel(manifest)} {reason}")
        return 1
    text = dumps(view)
    rest = args[1:]
    if rest[:1] == ["--check"] and len(rest) == 2:
        return 0 if check(text, pathlib.Path(rest[1])) else 1
    if rest == ["--write"]:
        fixture_for(view).write_text(text)
        print(f"wrote {rel(fixture_for(view))}")
        return 0
    if rest:
        print(f"unknown arguments: {' '.join(rest)}", file=sys.stderr)
        return 2
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
