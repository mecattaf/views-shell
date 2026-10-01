#!/usr/bin/env python3
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""example.echo: the reference T2 views-shell plugin (schemas/plugin-protocol.md).

Python 3 standard library only. One JSON-RPC 2.0 message per line on stdin and
stdout; stdout carries nothing but those lines, diagnostics go to stderr. The
loop never blocks on the host: a request this plugin sends (exec, notify) is
answered whenever the host gets to it, and a command/invoke that waits on such
an answer (try-exec) is completed when the answer arrives.

What it shows, method by method:
  initialize       answers {protocol: 1}, then sends both trees and a snapshot
  snapshot         {counter, next, greeting, lastEvent}, always whole, re-sent
                   after every change
  surface/setTree  "button" (a bar button bound to /counter whose action is
                   set-counter with args bound to /next) and "panel"
  command/invoke   ping (echoes its args, and sends a notify, which the
                   manifest declares), set-counter, try-exec (asks the broker to
                   run a program with no exec: permission and returns the
                   broker's answer, so the -32001 is visible)
  config/changed   applies greeting
  event            records {name, data} as lastEvent
  shutdown         answers {} and exits 0 at once; end of input exits 0 too
Unknown notifications are ignored; unknown requests get -32601; an undeclared
command gets -32602; a malformed line is dropped with a note on stderr.
"""
import json
import sys

PROTOCOL = 1
DEFAULT_GREETING = "hello"

state = {
    "counter": 0,
    "greeting": DEFAULT_GREETING,
    "lastEvent": {"name": "", "data": None},
}
# Our request id -> the host request id of the command/invoke waiting on it.
waiting = {}
_next_id = 0

BUTTON_TREE = {
    "schemaVersion": 1,
    "root": {
        "type": "button",
        "label": {"$bind": "/counter", "format": "text"},
        "icon": "add",
        "accessibleName": "Echo counter",
        "tooltip": {"$bind": "/greeting"},
        "action": {"command": "set-counter", "args": {"value": {"$bind": "/next"}}},
    },
}

PANEL_TREE = {
    "schemaVersion": 1,
    "root": {
        "type": "column",
        "container": "rounded",
        "spacing": "normal",
        "children": [
            {"type": "label", "text": {"$bind": "/greeting"}, "typography": "title"},
            {"type": "separator"},
            {
                "type": "row",
                "align": "center",
                "children": [
                    {"type": "label", "text": "Counter", "flex": 1},
                    {"type": "label", "text": {"$bind": "/counter", "format": "text"}},
                ],
            },
            {
                "type": "row",
                "align": "center",
                "children": [
                    {"type": "label", "text": "Last event", "flex": 1},
                    {"type": "label", "text": {"$bind": "/lastEvent/name"}, "role": "subtle"},
                ],
            },
            {
                "type": "row",
                "children": [
                    {"type": "button", "label": "Reset", "variant": "secondary",
                     "action": {"command": "set-counter", "args": {"value": 0}}},
                    {"type": "button", "label": "Add one", "variant": "primary",
                     "action": {"command": "set-counter", "args": {"value": {"$bind": "/next"}}}},
                ],
            },
        ],
    },
}


def log(text):
    print(f"example.echo: {text}", file=sys.stderr, flush=True)


def send(obj):
    sys.stdout.write(json.dumps(obj, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def notify(method, params):
    send({"jsonrpc": "2.0", "method": method, "params": params})


def request(method, params):
    global _next_id
    _next_id += 1
    rid = f"echo-{_next_id}"
    send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
    return rid


def reply(mid, result):
    send({"jsonrpc": "2.0", "id": mid, "result": result})


def error(mid, code, message):
    send({"jsonrpc": "2.0", "id": mid, "error": {"code": code, "message": message}})


def snapshot():
    notify("snapshot", {"data": {
        "counter": state["counter"],
        "next": state["counter"] + 1,
        "greeting": state["greeting"],
        "lastEvent": state["lastEvent"],
    }})


def apply_config(config):
    greeting = (config or {}).get("greeting", DEFAULT_GREETING)
    state["greeting"] = greeting if isinstance(greeting, str) else DEFAULT_GREETING


def invoke(mid, params):
    """command/invoke. Returns True when the reply is sent later (try-exec)."""
    command, args = params.get("command"), params.get("args") or {}
    if command == "ping":
        request("notify", {"summary": "Echo", "body": str(args.get("text", ""))})
        reply(mid, {"result": {"args": args}})
    elif command == "set-counter":
        value = args.get("value")
        if isinstance(value, bool) or not isinstance(value, int):
            error(mid, -32602, "set-counter: value must be an integer")
            return False
        state["counter"] = value
        reply(mid, {})
        snapshot()
    elif command == "try-exec":
        # exec:true is not in the manifest: the broker must refuse with -32001.
        waiting[request("exec", {"program": "true", "args": []})] = mid
        return True
    else:
        error(mid, -32602, f"unknown command {command!r}")
    return False


def on_answer(msg):
    """A host answer to one of our requests."""
    mid = waiting.pop(msg.get("id"), None)
    if "error" in msg:
        log(f"host answered {msg.get('id')} with error {msg['error']}")
    if mid is not None:
        answer = {"error": msg["error"]} if "error" in msg else {"result": msg.get("result")}
        reply(mid, {"result": {"program": "true", "answer": answer}})


def handle(msg):
    if not isinstance(msg, dict) or msg.get("jsonrpc") != "2.0":
        log("dropped a message that is not a JSON-RPC 2.0 object")
        return True
    method, mid = msg.get("method"), msg.get("id")
    params = msg.get("params") or {}
    if method is None:
        on_answer(msg)
        return True
    if method == "initialize":
        apply_config(params.get("config"))
        reply(mid, {"protocol": PROTOCOL})
        notify("surface/setTree", {"surface": "button", "tree": BUTTON_TREE})
        notify("surface/setTree", {"surface": "panel", "tree": PANEL_TREE})
        snapshot()
    elif method == "shutdown":
        if mid is not None:
            reply(mid, {})
        return False
    elif method == "command/invoke":
        invoke(mid, params)
    elif method == "config/changed":
        apply_config(params.get("config"))
        if mid is not None:
            reply(mid, {})
        snapshot()
    elif method in ("surface/opened", "surface/closed"):
        if mid is not None:
            reply(mid, {})
    elif method == "event":
        state["lastEvent"] = {"name": params.get("name", ""), "data": params.get("data")}
        snapshot()
    elif mid is not None:
        error(mid, -32601, f"method not found: {method}")
    # Any other notification is ignored.
    return True


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            log("dropped a malformed line")
            continue
        if not handle(msg):
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
