#!/usr/bin/env python3
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The reference render trace of a views-shell ui tree.

    tools/ui-tree-render.py <ui.json> [--snapshot <snapshot.json>] [--plugin <id>]
    tools/ui-tree-render.py <ui.json> --snapshot <s> --check <render fixture>
    tools/ui-tree-render.py <ui.json> --snapshot <s> --write
    tools/ui-tree-render.py --check-all | --write-all

Prints which stock Views class each node of a ui tree becomes and with which
properties, as JSON with sorted keys, two-space indent and a final newline. The
C++ ui-tree renderer (w3a) reproduces this trace byte for byte. The contract,
the view table and the formatting rules are schemas/ui-tree-rendering.md; this
file is its reference implementation.

The ui tree is validated first with tools/validate.py's ui-tree validator
(imported, never copied); an invalid tree prints `REJECT <file> <reason>` on
stdout and exits 1.

A snapshot file is {"snapshot": <the entry's state>, "sources": {"<plugin>/<source>":
<that source's snapshot>, ...}}. The plugin id, used to qualify bare command
names, comes from --plugin or from the views-shell-plugin.json two directories
up (examples/<dir>/ui/<file>.json).

Fixture names: tools/fixtures/snapshots/<example dir>.json and
tools/fixtures/render/<example dir>.<ui file name>.json. --check-all and
--write-all walk every examples/*/ui/*.json against its directory's snapshot,
fail on any {"$unresolved": ...} in a committed trace, require every
tools/fixtures/invalid/*.ui.json to REJECT, and print "render ok (N)".
"""
import datetime
import difflib
import json
import math
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
NIX_PYTHON = ('(builtins.getFlake "nixpkgs").legacyPackages.${builtins.currentSystem}'
              '.python3.withPackages (p: [ p.jsonschema ])')

# The fixed clock for relative-time, so traces are deterministic.
NOW = datetime.datetime(2026, 10, 2, 0, 0, 0, tzinfo=datetime.timezone.utc)
NOT_DRAWN = "not drawn in chapter 2"

# node type -> (stock Views class or None, layout or None). The contract table of
# schemas/ui-tree-rendering.md.
VIEWS = {
    "column": ("views::BoxLayoutView", "box-vertical"),
    "row": ("views::BoxLayoutView", "box-horizontal"),
    "stack": ("views::View", "fill"),
    "grid": ("views::TableLayoutView", "table"),
    "scroll": ("views::ScrollView", None),
    "repeat": (None, None),
    "label": ("views::Label", None),
    "icon": ("views::ImageView", None),
    "image": ("views::ImageView", None),
    "badge": ("views::Badge", None),
    "dot": ("views::DotIndicator", None),
    "separator": ("views::Separator", None),
    "spacer": ("views::View", None),
    "progress": ("views::ProgressBar", None),
    "button": ("views::MdTextButton", None),
    "iconButton": ("views::ImageButton", None),
    "switch": ("views::ToggleButton", None),
    "checkbox": ("views::Checkbox", None),
    "radioGroup": ("views::BoxLayoutView", "box-vertical"),
    "select": ("views::Combobox", None),
    "slider": ("views::Slider", None),
    "textfield": ("views::Textfield", None),
    "listItem": ("views_shell::ListItemView", None),
    "markdown": (None, None),
    "tile": (None, None),
    "tabSlider": (None, None),
    "keyChips": (None, None),
    "mediaSession": (None, None),
}
UNDRAWN = {"markdown", "tile", "tabSlider", "keyChips", "mediaSession"}
# Node-valued properties: they become children, in this order, never props.
CHILD_KEYS = ("children", "child", "template", "trailing")
ACTION_KEYS = ("action", "iconAction")


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
from validate import validator  # noqa: E402
from jsonschema.exceptions import best_match  # noqa: E402


def dumps(obj):
    return json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


# ---------------------------------------------------------------- formatting
def round_half_away(x):
    """std::round: halves away from zero, exact (no x + 0.5 rounding error)."""
    t = math.trunc(x)
    f = x - t
    return t + (1 if f >= 0.5 else -1 if f <= -0.5 else 0)


def is_number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def as_text(v):
    if v is None:
        return ""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, str):
        return v
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return str(int(v)) if v.is_integer() else repr(v)
    return json.dumps(v, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def as_instant(v):
    """Epoch seconds (a number) or an ISO 8601 string with an offset or Z."""
    if is_number(v):
        return datetime.datetime.fromtimestamp(v, datetime.timezone.utc)
    if isinstance(v, str):
        try:
            t = datetime.datetime.fromisoformat(v.replace("Z", "+00:00"))
        except ValueError:
            return None
        return t if t.tzinfo else None
    return None


def fmt_duration(v):
    s = math.floor(v)
    d, rem = divmod(s, 86400)
    h, rem = divmod(rem, 3600)
    m, sec = divmod(rem, 60)
    if d:
        return f"{d} d {h} h"
    if h:
        return f"{h} h {m} min"
    if m:
        return f"{m} min"
    return f"{sec} s"


def fmt_bytes(v):
    n = math.floor(v)
    if n < 1024:
        return f"{n} B"
    value, unit = float(n), "B"
    for unit in ("KB", "MB", "GB", "TB", "PB"):
        value /= 1024
        if value < 1024 or unit == "PB":
            break
    if value < 100:
        tenths = round_half_away(value * 10)
        return f"{tenths // 10}.{tenths % 10} {unit}"
    return f"{round_half_away(value)} {unit}"


def fmt_relative(t):
    delta = math.floor((NOW - t).total_seconds())
    span = abs(delta)
    if span < 60:
        return "now"
    if span < 3600:
        text = f"{span // 60} min"
    elif span < 86400:
        text = f"{span // 3600} h"
    else:
        text = f"{span // 86400} d"
    return f"{text} ago" if delta > 0 else f"in {text}"


def apply_format(fmt, v):
    if fmt is None:
        return v
    if fmt in ("percent", "duration", "bytes") and is_number(v):
        if fmt == "percent":
            return f"{round_half_away(v * 100)}%"
        return fmt_duration(v) if fmt == "duration" else fmt_bytes(v)
    if fmt in ("time", "relative-time"):
        t = as_instant(v)
        if t is not None:
            if fmt == "time":
                return t.astimezone(datetime.timezone.utc).strftime("%H:%M")
            return fmt_relative(t)
    return as_text(v)


# ---------------------------------------------------------------- resolution
MISSING = object()


def pointer(base, ptr):
    """RFC 6901 evaluation; MISSING when a token does not resolve."""
    if ptr == "":
        return base
    cur = base
    for raw in ptr.split("/")[1:]:
        tok = raw.replace("~1", "/").replace("~0", "~")
        if isinstance(cur, dict) and tok in cur:
            cur = cur[tok]
        elif (isinstance(cur, list) and tok.isdigit()
              and (tok == "0" or not tok.startswith("0")) and int(tok) < len(cur)):
            cur = cur[int(tok)]
        else:
            return MISSING
    return cur


def is_binding(v):
    return isinstance(v, dict) and "$bind" in v


class Context:
    def __init__(self, data, plugin_id):
        # data is None when no snapshot was given: bindings stay as written.
        self.data = data
        self.plugin_id = plugin_id
        self.item = MISSING

    def resolve(self, b):
        if self.data is None:
            return dict(b)
        ptr = b["$bind"]
        if "source" in b:
            base = self.data.get("sources", {}).get(b["source"], MISSING)
        elif ptr.startswith("."):
            base, ptr = self.item, ptr[1:]
        else:
            base = self.data.get("snapshot", MISSING)
        value = MISSING if base is MISSING else pointer(base, ptr)
        if value is MISSING:
            return {"$unresolved": b["$bind"]}
        return apply_format(b.get("format"), value)

    def value(self, v):
        """A property value: bindings resolved, containers walked."""
        if is_binding(v):
            return self.resolve(v)
        if isinstance(v, dict):
            return {k: self.value(x) for k, x in v.items()}
        if isinstance(v, list):
            return [self.value(x) for x in v]
        return v

    def action(self, a):
        command = self.value(a["command"])
        if isinstance(command, str) and "/" not in command and self.plugin_id:
            command = f"{self.plugin_id}/{command}"
        return {
            "command": command,
            "args": self.value(a.get("args", {})),
            "confirm": self.value(a["confirm"]) if "confirm" in a else None,
            "close": a.get("close", False),
        }


# ---------------------------------------------------------------- the trace
def trace(node, ctx, single_slot):
    """The trace of one node. single_slot: the node fills a one-child slot (the
    root, scroll.child, listItem.trailing), where an expanded repeat needs a
    container of its own."""
    kind = node["type"]
    view, layout = VIEWS[kind]
    props = {}
    for k, v in node.items():
        if k == "type" or k in CHILD_KEYS or k == "$schema":
            continue
        props[k] = ctx.action(v) if k in ACTION_KEYS else ctx.value(v)
    out = {"node": kind, "view": view, "layout": layout, "props": props,
           "children": [], "error": NOT_DRAWN if kind in UNDRAWN else None}

    if kind == "repeat":
        direction = node.get("direction", "column")
        if single_slot:
            out["view"] = "views::BoxLayoutView"
            out["layout"] = "box-vertical" if direction == "column" else "box-horizontal"
        props["items"] = dict(node["items"])
        items = ctx.resolve(node["items"]) if ctx.data is not None else MISSING
        if ctx.data is None:
            out["children"] = [trace(node["template"], ctx, False)]
        elif not isinstance(items, list):
            props["count"] = 0
            out["error"] = f"items does not resolve to an array: {node['items']['$bind']}"
        else:
            props["count"] = len(items)
            saved = ctx.item
            keys = []
            for item in items:
                ctx.item = item
                if "key" in node:
                    keys.append(ctx.resolve({"$bind": node["key"]}))
                out["children"].append(trace(node["template"], ctx, False))
            ctx.item = saved
            if "key" in node:
                props["keys"] = keys
        return out

    if kind == "radioGroup":
        options, value = props.get("options"), props.get("value")
        if isinstance(options, list):
            for o in options:
                if not isinstance(o, dict):
                    continue
                out["children"].append({
                    "node": "option", "view": "views::RadioButton", "layout": None,
                    "props": {**o, "checked": o.get("value") == value},
                    "children": [], "error": None})

    for k in CHILD_KEYS:
        if k not in node:
            continue
        if k == "children":
            out["children"] += [trace(c, ctx, False) for c in node[k]]
        else:
            out["children"].append(trace(node[k], ctx, True))
    return out


def render(tree, data, plugin_id):
    ctx = Context(data, plugin_id)
    return {"plugin": plugin_id, "root": trace(tree["root"], ctx, True)}


def has_unresolved(v):
    if isinstance(v, dict):
        return "$unresolved" in v or any(has_unresolved(x) for x in v.values())
    if isinstance(v, list):
        return any(has_unresolved(x) for x in v)
    return False


# ---------------------------------------------------------------- the CLI
def rel(p):
    p = p.resolve()
    return p.relative_to(ROOT) if p.is_relative_to(ROOT) else p


def plugin_id_for(ui_path):
    manifest = ui_path.resolve().parent.parent / "views-shell-plugin.json"
    try:
        return json.loads(manifest.read_text())["id"]
    except (OSError, ValueError, KeyError):
        return None


def evaluate(ui_path, ui_v, snapshot_path, plugin_id):
    """(trace, None) or (None, reason)."""
    try:
        tree = json.loads(ui_path.read_text())
    except (OSError, json.JSONDecodeError) as e:
        return None, f"not JSON: {e}"
    e = best_match(ui_v.iter_errors(tree))
    if e is not None:
        return None, f"{'/'.join(map(str, e.path)) or '(root)'}: {e.message[:300]}"
    data = json.loads(snapshot_path.read_text()) if snapshot_path else None
    return render(tree, data, plugin_id or plugin_id_for(ui_path)), None


def check(text, fixture):
    want = fixture.read_text() if fixture.is_file() else ""
    if text == want:
        return True
    sys.stderr.writelines(difflib.unified_diff(
        want.splitlines(True), text.splitlines(True), str(rel(fixture)), "render trace"))
    return False


def fixture_paths(ui_path):
    d = ui_path.resolve().parent.parent.name
    return (ROOT / "tools/fixtures/snapshots" / f"{d}.json",
            ROOT / "tools/fixtures/render" / f"{d}.{ui_path.name}")


def run_all(write):
    ui_v = validator("ui-tree")
    ok, n = True, 0
    for ui_path in sorted(ROOT.glob("examples/*/ui/*.json")):
        snap, fixture = fixture_paths(ui_path)
        if not snap.is_file():
            print(f"FAIL render {rel(ui_path)}: no snapshot {rel(snap)}", file=sys.stderr)
            ok = False
            continue
        out, reason = evaluate(ui_path, ui_v, snap, None)
        if out is None:
            print(f"REJECT {rel(ui_path)} {reason}", file=sys.stderr)
            ok = False
            continue
        if has_unresolved(out):
            print(f"FAIL render {rel(ui_path)}: a binding does not resolve against "
                  f"{rel(snap)}", file=sys.stderr)
            ok = False
        text = dumps(out)
        if write:
            fixture.write_text(text)
        elif not check(text, fixture):
            print(f"FAIL render {rel(ui_path)} differs from {rel(fixture)}", file=sys.stderr)
            ok = False
        n += 1
    rejected = 0
    for bad in sorted(ROOT.glob("tools/fixtures/invalid/*.ui.json")):
        out, reason = evaluate(bad, ui_v, None, None)
        if out is not None:
            print(f"FAIL render {rel(bad)} was rendered", file=sys.stderr)
            ok = False
        else:
            print(f"REJECT {rel(bad)} {reason}")
            rejected += 1
    if ok:
        print(f"render ok ({n})")
        print(f"render rejects ({rejected})")
    return 0 if ok else 1


def main(argv):
    args = argv[1:]
    if args in (["--check-all"], ["--write-all"]):
        return run_all(args[0] == "--write-all")
    if not args or args[0].startswith("-"):
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    ui_path = pathlib.Path(args[0])
    opts, mode, rest = {}, None, args[1:]
    while rest:
        flag = rest.pop(0)
        if flag in ("--snapshot", "--plugin", "--check") and rest:
            opts[flag] = rest.pop(0)
        elif flag == "--write":
            mode = "write"
        else:
            print(f"unknown argument: {flag}", file=sys.stderr)
            return 2
    snap = pathlib.Path(opts["--snapshot"]) if "--snapshot" in opts else None
    out, reason = evaluate(ui_path, validator("ui-tree"), snap, opts.get("--plugin"))
    if out is None:
        print(f"REJECT {rel(ui_path)} {reason}")
        return 1
    text = dumps(out)
    if "--check" in opts:
        return 0 if check(text, pathlib.Path(opts["--check"])) else 1
    if mode == "write":
        fixture = fixture_paths(ui_path)[1]
        fixture.write_text(text)
        print(f"wrote {rel(fixture)}")
        return 0
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
