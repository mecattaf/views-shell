#!/usr/bin/env python3
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Plugin-protocol conformance runner: plays views-shell against one T2 plugin.

  python3 tools/plugin-conformance.py <plugin-dir>

It checks the manifest with tools/validate.py's own functions, spawns
runtime.exec (plus runtime.args) with the plugin directory as the working
directory, and drives one scripted session of schemas/plugin-protocol.md,
protocol 1:

  initialize      the answer is {protocol: 1}
  startup         a snapshot and one surface/setTree per contributed surface
                  (every surface except kinds service and picker-provider, plus
                  every quick-settings entry) within the startup window; every
                  tree validates against schemas/ui-tree.schema.json, names only
                  commands the plugin declares (or covers with call:), and every
                  binding resolves in the latest snapshot
  surfaces        surface/opened and surface/closed for each surface answer {}
  commands        command/invoke for each declared command the plugin handles
                  (a command with a verb is views-shell's own), with valid args;
                  the answer matches the declared result (none: {}, text:
                  {result: string}, json: {result: <any>})
  unknown command command/invoke of an undeclared command answers -32602
  config          config/changed with every declared property changed answers {}
  events          one event notification per contributes.events entry
  robustness      a malformed line, an unknown notification and an unknown
                  request (answered -32601) leave the plugin alive
  broker          the runner answers every plugin request: a declared one with a
                  canned success (nothing is ever run), an undeclared one with
                  -32001, after which the plugin must still be alive
  shutdown        answers {} and the process exits 0 within the grace period
  stream          stdout carried only JSON-RPC 2.0 objects, one per line, only
                  protocol methods, and no answer to an id the runner never sent

One line per check, then CONFORMANCE-OK <id> (exit 0) or
CONFORMANCE-FAILED <id> <reason> (exit 1). Needs the python `jsonschema`
package, as tools/validate.py does. Nothing the plugin asks for is executed.
"""
import json
import pathlib
import queue
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
try:
    import validate  # tools/validate.py: manifest_problems, validator, commands_in, covered
except ModuleNotFoundError as e:
    sys.exit(f"plugin-conformance: {e}; run it with a python3 that has jsonschema, as "
             "tools/validate.sh does (nix shell nixpkgs#python3Packages.jsonschema -c ...)")

PROTOCOL = 1
WINDOW = 2.0  # seconds: the startup window and the answer deadline for any request
GRACE = 1.0   # seconds from the shutdown request to process exit
TREELESS_KINDS = {"service", "picker-provider"}
PLUGIN_NOTIFICATIONS = {"snapshot", "surface/setTree", "source/snapshot"}
PLUGIN_REQUESTS = {"notify", "toast", "exec", "compositor/command"}
SAMPLE_ARG = {"string": "conformance", "integer": 7, "number": 0.5, "boolean": True}
MISSING = object()


def permission_for(method, params):
    """The permission a plugin request needs, or None when it needs none."""
    if method == "exec":
        return f"exec:{params.get('program')}"
    if method == "notify":
        return "notifications"
    if method == "compositor/command":
        return f"compositor:{params.get('capability')}"
    return None


def canned_result(method):
    if method == "exec":
        return {"exitCode": 0, "stdout": "", "stderr": ""}
    return {}


def changed_value(prop):
    """A valid value for a configuration property that differs from its default."""
    default = prop.get("default", MISSING)
    if "enum" in prop:
        others = [v for v in prop["enum"] if v != default]
        return others[0] if others else default
    kind = prop["type"]
    if kind == "boolean":
        return not default if isinstance(default, bool) else True
    if kind in ("integer", "number"):
        value = (default if isinstance(default, (int, float)) else 0) + 1
        if "maximum" in prop and value > prop["maximum"]:
            value = prop.get("minimum", prop["maximum"] - 1)
        return value
    if kind == "array":
        return []
    return f"{default if isinstance(default, str) else ''} (conformance)".strip()


def resolve(pointer, root, item):
    """RFC 6901 pointer into the snapshot; ./ pointers into the repeat item."""
    if pointer.startswith("."):
        if item is MISSING:
            return MISSING
        doc, pointer = item, pointer[1:]
    else:
        doc = root
    for token in pointer.split("/")[1:]:
        token = token.replace("~1", "/").replace("~0", "~")
        if isinstance(doc, dict) and token in doc:
            doc = doc[token]
        elif isinstance(doc, list) and token.isdigit() and int(token) < len(doc):
            doc = doc[int(token)]
        else:
            return MISSING
    return doc


def unresolved_bindings(node, snapshot, item=MISSING, out=None):
    """Every binding in a tree that does not resolve in the snapshot."""
    out = [] if out is None else out
    if isinstance(node, dict):
        if "$bind" in node:
            if "source" not in node and resolve(node["$bind"], snapshot, item) is MISSING:
                out.append(node["$bind"])
            return out
        if node.get("type") == "repeat" and isinstance(node.get("items"), dict):
            items = resolve(node["items"].get("$bind", ""), snapshot, item)
            if "source" in node["items"]:
                return out
            if not isinstance(items, list):
                out.append(f"{node['items'].get('$bind')} (repeat items, not an array)")
                return out
            for each in items:
                unresolved_bindings(node.get("template"), snapshot, each, out)
            return out
        for v in node.values():
            unresolved_bindings(v, snapshot, item, out)
    elif isinstance(node, list):
        for v in node:
            unresolved_bindings(v, snapshot, item, out)
    return out


class Session:
    """One plugin process and the host side of its JSON-RPC stream."""

    def __init__(self, manifest, base):
        self.m = manifest
        self.base = base
        self.perms = set(manifest.get("permissions", []))
        self.lines = queue.Queue()
        self.responses = {}
        self.sent_ids = set()
        self.snapshot = MISSING
        self.trees = {}
        self.broker_log = []       # (method, permission or None, error code or None)
        self.violations = []
        self.messages = 0
        self.eof = False
        self.initialized = False
        self._next = 0
        self.stderr = tempfile.TemporaryFile()
        rt = manifest["runtime"]
        argv = [str(base / rt["exec"])] + rt.get("args", [])
        self.proc = subprocess.Popen(argv, cwd=base, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=self.stderr)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for raw in iter(self.proc.stdout.readline, b""):
            self.lines.put(raw)
        self.lines.put(None)

    # Writing.
    def write_raw(self, data):
        try:
            self.proc.stdin.write(data)
            self.proc.stdin.flush()
            return True
        except (BrokenPipeError, ValueError):
            return False

    def write(self, obj):
        return self.write_raw((json.dumps(obj, separators=(",", ":")) + "\n").encode())

    def notify(self, method, params):
        return self.write({"jsonrpc": "2.0", "method": method, "params": params})

    def call(self, method, params, timeout=WINDOW):
        """Sends a request and pumps until its answer; returns the message or None."""
        self._next += 1
        rid = f"host-{self._next}"
        self.sent_ids.add(rid)
        if not self.write({"jsonrpc": "2.0", "id": rid, "method": method, "params": params}):
            return None
        self.pump(lambda: rid in self.responses, timeout)
        return self.responses.pop(rid, None)

    # Reading.
    def pump(self, done, timeout):
        deadline = time.monotonic() + timeout
        while not done():
            left = deadline - time.monotonic()
            if left <= 0 or self.eof:
                return False
            try:
                raw = self.lines.get(timeout=left)
            except queue.Empty:
                return False
            if raw is None:
                self.eof = True
                return done()
            self._take(raw)
        return True

    def _violate(self, text):
        self.violations.append(text)

    def _take(self, raw):
        try:
            line = raw.decode("utf-8").strip()
        except UnicodeDecodeError:
            self._violate("stdout line is not UTF-8")
            return
        if not line:
            return
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            self._violate(f"stdout line is not JSON: {line[:80]!r}")
            return
        self.messages += 1
        if not isinstance(msg, dict) or msg.get("jsonrpc") != "2.0":
            self._violate(f"not a JSON-RPC 2.0 object: {line[:80]!r}")
            return
        method = msg.get("method")
        if method is None:
            rid = msg.get("id")
            if rid not in self.sent_ids or rid in self.responses:
                self._violate(f"answer to an id the host never sent (or answered twice): {rid!r}")
                return
            if ("result" in msg) == ("error" in msg):
                self._violate(f"answer {rid!r} carries neither or both of result and error")
            self.responses[rid] = msg
            return
        if not self.initialized:
            self._violate(f"{method} sent before the initialize answer")
        params = msg.get("params") if isinstance(msg.get("params"), dict) else {}
        if "id" in msg:
            self._broker(msg["id"], method, params)
        elif method in PLUGIN_REQUESTS:
            self._violate(f"{method} sent as a notification; it is a request (needs an id)")
        elif method == "snapshot":
            if isinstance(params.get("data"), dict):
                self.snapshot = params["data"]
            else:
                self._violate("snapshot without an object data")
        elif method == "surface/setTree":
            self.trees[params.get("surface")] = params.get("tree")
        elif method == "source/snapshot":
            declared = {s.get("id") for s in self.m.get("contributes", {}).get("sources", [])}
            if params.get("source") not in declared:
                self._violate(f"source/snapshot for undeclared source {params.get('source')!r}")
        else:
            self._violate(f"unknown notification {method!r}")

    def _broker(self, rid, method, params):
        def answer_error(code, text):
            self.write({"jsonrpc": "2.0", "id": rid, "error": {"code": code, "message": text}})
        if method not in PLUGIN_REQUESTS:
            self._violate(f"unknown request {method!r}")
            self.broker_log.append((method, None, -32601))
            answer_error(-32601, f"method not found: {method}")
            return
        if method == "exec" and not (isinstance(params.get("program"), str) and
                                     isinstance(params.get("args", []), list)):
            self._violate("exec without a program string and an args array")
            self.broker_log.append((method, None, -32602))
            answer_error(-32602, "invalid params")
            return
        perm = permission_for(method, params)
        if perm is not None and perm not in self.perms:
            self.broker_log.append((method, perm, -32001))
            answer_error(-32001, "permission not declared")
            return
        self.broker_log.append((method, perm, None))
        self.write({"jsonrpc": "2.0", "id": rid, "result": canned_result(method)})

    def alive(self):
        """The liveness probe: an unknown request must be answered -32601."""
        ans = self.call("conformance/unknown-method", {})
        return ans is not None and ans.get("error", {}).get("code") == -32601

    def stderr_tail(self, n=12):
        self.stderr.seek(0)
        return self.stderr.read().decode("utf-8", "replace").splitlines()[-n:]

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()


class Report:
    def __init__(self, pid):
        self.pid = pid
        self.failures = []

    def check(self, name, ok, detail=""):
        tail = f": {detail}" if detail and not ok else ""
        print(f"{'ok  ' if ok else 'FAIL'} {self.pid} {name}{tail}", flush=True)
        if not ok:
            self.failures.append(f"{name}{tail}")
        return ok


def expected_surfaces(contributes):
    ids = [s["id"] for s in contributes.get("surfaces", []) if s["kind"] not in TREELESS_KINDS]
    return ids + [q["id"] for q in contributes.get("quickSettings", [])]


def tree_problems(manifest, tree, ui_v):
    problems = [f"{'/'.join(map(str, e.path))}: {e.message[:160]}"
                for e in ui_v.iter_errors(tree)]
    ids = {c["id"] for c in manifest.get("contributes", {}).get("commands", [])}
    perms = set(manifest.get("permissions", []))
    for cmd in validate.commands_in(tree, []):
        if "/" in cmd:
            if not validate.covered(manifest["id"], perms, cmd):
                problems.append(f"calls {cmd} without a call: permission")
        elif cmd not in ids:
            problems.append(f"names undeclared command {cmd}")
    return problems


def run(base):
    manifest_path = base / "views-shell-plugin.json"
    if not manifest_path.is_file():
        print(f"CONFORMANCE-FAILED {base.name} no views-shell-plugin.json in {base}")
        return 1
    m = validate.load(manifest_path)
    pid = m.get("id", base.name)
    report = Report(pid)
    plugin_v, ui_v = validate.validator("views-shell-plugin"), validate.validator("ui-tree")
    schema_errs, other = validate.manifest_problems(manifest_path, plugin_v)
    probs = schema_errs + other
    report.check("manifest (tools/validate.py checks)", not probs, "; ".join(probs[:3]))
    rt = m.get("runtime", {})
    report.check("runtime.mode process, engines.protocol 1",
                 rt.get("mode") == "process" and m.get("engines", {}).get("protocol") == PROTOCOL,
                 f"mode {rt.get('mode')!r}, protocol {m.get('engines', {}).get('protocol')!r}")
    if report.failures:
        print(f"CONFORMANCE-FAILED {pid} {report.failures[0]}")
        return 1

    c = m.get("contributes", {})
    config = {k: p["default"] for k, p in c.get("configuration", {}).get("properties", {}).items()
              if "default" in p}
    caps = sorted(set(m.get("requires", {}).get("compositor", {}).get("required", [])) |
                  set(m.get("requires", {}).get("compositor", {}).get("optional", [])))
    try:
        s = Session(m, base)
    except OSError as e:
        print(f"CONFORMANCE-FAILED {pid} cannot start {rt['exec']}: {e}")
        return 1
    try:
        return drive(s, m, c, config, caps, ui_v, report, pid)
    finally:
        s.close()


def drive(s, m, c, config, caps, ui_v, report, pid):
    started = time.monotonic()
    ans = s.call("initialize", {"protocol": PROTOCOL, "shellVersion": "0.1.0", "pluginId": pid,
                                "capabilities": caps, "config": config, "locale": "en-US"})
    s.initialized = True
    result = ans.get("result") if ans else None
    if not report.check("initialize answers {protocol: 1}",
                        isinstance(result, dict) and result.get("protocol") == PROTOCOL,
                        f"answer {json.dumps(ans)[:160] if ans else 'none within 2 s'}"):
        return finish(s, report, pid)

    surfaces = expected_surfaces(c)
    s.pump(lambda: s.snapshot is not MISSING and all(x in s.trees for x in surfaces),
           max(0.0, started + WINDOW - time.monotonic()))
    missing = [x for x in surfaces if x not in s.trees]
    report.check(f"startup: a snapshot and {len(surfaces)} surface/setTree within {WINDOW:g} s",
                 s.snapshot is not MISSING and not missing,
                 f"snapshot {'seen' if s.snapshot is not MISSING else 'missing'}, trees missing {missing}")
    extra = [x for x in s.trees if x not in surfaces]
    report.check("setTree names only contributed surfaces", not extra, f"unknown {extra}")
    for surface in surfaces:
        if surface in s.trees:
            p = tree_problems(m, s.trees[surface], ui_v)
            report.check(f"tree {surface}: ui-tree schema and commands", not p, "; ".join(p[:3]))
    check_bindings(s, report, "startup")

    for surface in (x["id"] for x in c.get("surfaces", [])):
        for verb in ("surface/opened", "surface/closed"):
            ans = s.call(verb, {"surface": surface, "instance": "conformance-1"})
            report.check(f"{verb} {surface} answers {{}}", ans is not None and ans.get("result") == {},
                         json.dumps(ans)[:160] if ans else "no answer within 2 s")

    for cmd in c.get("commands", []):
        if "verb" in cmd:
            print(f"skip {pid} command {cmd['id']}: verb {cmd['verb']} is handled by views-shell")
            continue
        args = {a["name"]: SAMPLE_ARG[a["type"]] for a in cmd.get("args", [])}
        before = len(s.broker_log)
        ans = s.call("command/invoke", {"command": cmd["id"], "args": args, "source": "cli"})
        kind = cmd.get("result", "none")
        res = ans.get("result") if ans else None
        if kind == "none":
            ok = res == {}
        elif kind == "text":
            ok = isinstance(res, dict) and isinstance(res.get("result"), str)
        else:
            ok = isinstance(res, dict) and "result" in res
        asked = ", ".join(f"{m_}{' ' + p if p else ''}{' -> ' + str(e) if e else ''}"
                          for m_, p, e in s.broker_log[before:])
        report.check(f"command/invoke {cmd['id']} answers a {kind} result"
                     + (f" (asked the broker: {asked})" if asked else ""), ok,
                     json.dumps(ans)[:200] if ans else "no answer within 2 s")

    ans = s.call("command/invoke", {"command": "conformance-undeclared", "args": {}, "source": "cli"})
    report.check("command/invoke of an undeclared command answers -32602",
                 ans is not None and ans.get("error", {}).get("code") == -32602,
                 json.dumps(ans)[:160] if ans else "no answer within 2 s")

    props = c.get("configuration", {}).get("properties", {})
    changed = dict(config, **{k: changed_value(p) for k, p in props.items()})
    ans = s.call("config/changed", {"config": changed})
    report.check(f"config/changed ({len(props)} properties changed) answers {{}}",
                 ans is not None and ans.get("result") == {},
                 json.dumps(ans)[:160] if ans else "no answer within 2 s")

    for name in c.get("events", []):
        s.notify("event", {"name": name, "data": {"conformance": True}})
        report.check(f"event {name} delivered, plugin alive", s.alive(), "no -32601 to the probe")

    s.write_raw(b"{this is not json\n")
    s.write_raw(b"[1,2]\n")
    report.check("malformed lines (not JSON; JSON but not an object) dropped, plugin alive",
                 s.alive(), "no -32601 to the probe")
    s.notify("conformance/unknown-notification", {})
    report.check("unknown notification ignored, unknown request answers -32601", s.alive(),
                 "no -32601 to the probe")

    refused = [(meth, p) for meth, p, e in s.broker_log if e == -32001]
    granted = [(meth, p) for meth, p, e in s.broker_log if e is None]
    if refused:
        report.check(f"broker refused {', '.join(p for _, p in refused)} with -32001, plugin alive",
                     s.alive(), "no -32601 to the probe after the refusal")
    else:
        print(f"ok   {pid} broker: no undeclared request; {len(granted)} declared requests answered")

    check_bindings(s, report, "final")

    t0 = time.monotonic()
    ans = s.call("shutdown", {}, timeout=GRACE)
    report.check("shutdown answers {}", ans is not None and ans.get("result") == {},
                 json.dumps(ans)[:160] if ans else f"no answer within {GRACE:g} s")
    try:
        s.proc.stdin.close()
    except (BrokenPipeError, OSError):
        pass
    try:
        rc = s.proc.wait(timeout=max(0.0, t0 + GRACE - time.monotonic()))
    except subprocess.TimeoutExpired:
        rc = None
    report.check(f"process exits 0 within {GRACE:g} s of shutdown", rc == 0,
                 "still running" if rc is None else f"exit status {rc}")
    s.pump(lambda: False, 0.2)  # drain whatever was written before the exit
    report.check(f"stream: {s.messages} messages, all JSON-RPC 2.0 protocol traffic",
                 not s.violations, "; ".join(s.violations[:3]))
    return finish(s, report, pid)


def check_bindings(s, report, when):
    if s.snapshot is MISSING:
        return
    for surface, tree in s.trees.items():
        bad = unresolved_bindings(tree, s.snapshot)
        report.check(f"tree {surface}: bindings resolve in the {when} snapshot", not bad,
                     f"unresolved {bad[:4]}")


def finish(s, report, pid):
    if report.failures:
        for line in s.stderr_tail():
            print(f"     {pid} stderr: {line}")
        print(f"CONFORMANCE-FAILED {pid} {report.failures[0]}")
        return 1
    print(f"CONFORMANCE-OK {pid}")
    return 0


def main(argv):
    if len(argv) != 2:
        print("usage: plugin-conformance.py <plugin-dir>", file=sys.stderr)
        return 2
    return run(pathlib.Path(argv[1]).resolve())


if __name__ == "__main__":
    sys.exit(main(sys.argv))
