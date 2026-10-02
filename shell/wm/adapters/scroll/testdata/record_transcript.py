#!/usr/bin/env python3
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Record an i3-ipc transcript from a running scroll (or sway) for the adapter tests.

Run it as the client of tools/bench/worker/headless.sh, which starts a headless
stock scroll under runtime-test and exports SCROLLSOCK:

  headless.sh <outdir> ~/views-bench/build-env -c \
    'python3 <this file> --out <outdir>/transcript.jsonl -- <window client cmd...>'

It speaks the protocol the scroll adapter speaks (two connections, subscribe
first, then GET_VERSION, GET_OUTPUTS, GET_WORKSPACES, GET_TREE) and then drives
one epoch per command: RUN_COMMAND, SEND_TICK with a unique payload, every event
up to the matching tick, then GET_OUTPUTS, GET_WORKSPACES and GET_TREE. The
command strings are the adapter's own spellings (scroll_adapter.cc), so the
unit test's fake server can check them byte for byte. The last command is
`exit`, which records the `shutdown` event and the end of the stream.

The optional window client (anything that maps one xdg_toplevel) is started
first and waited for, so the first epoch already holds a window.

Output: JSON Lines, one frame per line, {"dir", "conn", "type", "payload"}
(testdata/README.md). Every /home/<user>/ prefix is written as ~/ so the file
passes tools/check-fences.sh.
"""
import argparse
import json
import os
import re
import select
import socket
import struct
import subprocess
import sys
import time

MAGIC = b"i3-ipc"
RUN_COMMAND, GET_WORKSPACES, SUBSCRIBE, GET_OUTPUTS, GET_TREE = 0, 1, 2, 3, 4
GET_VERSION, SEND_TICK = 7, 10
EVENT_BIT = 0x80000000
TICK_EVENT = EVENT_BIT | 7
SUBSCRIPTIONS = ["workspace", "window", "output", "binding", "shutdown", "tick"]
# Only a path that starts at /home: not a later "/home/" inside it.
HOME_RE = re.compile(r"(?<![A-Za-z0-9._~-])/home/[A-Za-z0-9._-]+/")


def scrub(value):
    if isinstance(value, str):
        return HOME_RE.sub("~/", value)
    if isinstance(value, list):
        return [scrub(v) for v in value]
    if isinstance(value, dict):
        return {k: scrub(v) for k, v in value.items()}
    return value


class Recorder:
    def __init__(self, out):
        self.out = out
        self.lines = 0

    def log(self, direction, conn, ty, payload):
        rec = {"dir": direction, "conn": conn, "type": ty, "payload": scrub(payload)}
        self.out.write(json.dumps(rec, separators=(",", ":")) + "\n")
        self.out.flush()
        self.lines += 1


def socket_path():
    for name in ("SCROLLSOCK", "SWAYSOCK", "I3SOCK"):
        if os.environ.get(name):
            return os.environ[name]
    sys.exit("record_transcript: no SCROLLSOCK, SWAYSOCK or I3SOCK")


def connect():
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(socket_path())
    return s


def recvall(s, n):
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise EOFError
        buf += chunk
    return buf


def read_frame(s):
    hdr = recvall(s, 14)
    if hdr[:6] != MAGIC:
        raise ValueError("bad magic")
    length, ty = struct.unpack("=II", hdr[6:14])
    payload = recvall(s, length) if length else b""
    return ty, (json.loads(payload.decode()) if payload else None)


def write_frame(s, ty, payload):
    s.sendall(MAGIC + struct.pack("=II", len(payload), ty) + payload)


class Session:
    def __init__(self, rec):
        self.rec = rec
        self.req = connect()
        self.ev = connect()

    def subscribe(self):
        body = json.dumps(SUBSCRIPTIONS, separators=(",", ":")).encode()
        self.rec.log("c2s", "events", SUBSCRIBE, SUBSCRIPTIONS)
        write_frame(self.ev, SUBSCRIBE, body)
        ty, reply = read_frame(self.ev)
        self.rec.log("s2c", "events", ty, reply)

    def request(self, ty, payload=None, raw=False):
        if payload is None:
            body = b""
        elif raw:
            body = payload.encode()
        else:
            body = json.dumps(payload, separators=(",", ":")).encode()
        self.rec.log("c2s", "requests", ty, payload)
        write_frame(self.req, ty, body)
        rty, reply = read_frame(self.req)
        self.rec.log("s2c", "requests", rty, reply)
        return reply

    def drain_events(self, until_tick=None, timeout=5.0, until_eof=False):
        """Record events until the tick whose payload is until_tick, EOF, or timeout."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            r, _, _ = select.select([self.ev], [], [], max(0.0, deadline - time.time()))
            if not r:
                break
            try:
                ty, payload = read_frame(self.ev)
            except EOFError:
                return "eof"
            self.rec.log("s2c", "events", ty, payload)
            if (until_tick is not None and ty == TICK_EVENT and isinstance(payload, dict)
                    and payload.get("payload") == until_tick):
                return "tick"
        return "eof-expected" if until_eof else "timeout"

    def snapshot(self):
        self.request(GET_OUTPUTS)
        self.request(GET_WORKSPACES)
        return self.request(GET_TREE)


def leaves(node):
    kids = node.get("nodes", []) + node.get("floating_nodes", [])
    if not kids and node.get("type") in ("con", "floating_con") and (
            node.get("pid") or node.get("app_id") or node.get("window_properties")):
        yield node
    for kid in kids:
        yield from leaves(kid)


def quote(name):
    return '"' + name.replace("\\", "\\\\").replace('"', '\\"') + '"'


def wait_for_window(timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        s = connect()
        write_frame(s, GET_TREE, b"")
        _, tree = read_frame(s)
        s.close()
        found = list(leaves(tree))
        if found:
            return found[0]
        time.sleep(0.25)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--window-timeout", type=float, default=40.0)
    ap.add_argument("window_cmd", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = [a for a in args.window_cmd if a != "--"]

    child = None
    if cmd:
        log = open(os.path.join(os.path.dirname(os.path.abspath(args.out)), "window-client.log"), "w")
        child = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
        window = wait_for_window(args.window_timeout)
        print(f"RECORD window: {window and window.get('id')} app_id {window and window.get('app_id')}", flush=True)

    with open(args.out, "w") as out:
        rec = Recorder(out)
        s = Session(rec)
        s.subscribe()
        s.drain_events(timeout=0.5)  # the first tick ({"first": true}) arrives here
        s.request(GET_VERSION)
        tree = s.snapshot()
        found = list(leaves(tree))
        wid = found[0]["id"] if found else None
        commands = ["workspace --no-auto-back-and-forth " + quote("3"),
                    "rename workspace " + quote("3") + " to " + quote("web")]
        if wid is not None:
            commands += [f"[con_id={wid}] move container to workspace " + quote("web"),
                         f"[con_id={wid}] focus"]
        commands += ["scratchpad show",
                     "workspace --no-auto-back-and-forth " + quote("1"),
                     "reload"]
        for i, command in enumerate(commands, 1):
            reply = s.request(RUN_COMMAND, command, raw=True)
            tick = f"views-shell-rec-{i}"
            s.request(SEND_TICK, tick, raw=True)
            how = s.drain_events(until_tick=tick)
            s.snapshot()
            print(f"RECORD epoch {i}: {command!r} -> {json.dumps(reply)} ({how})", flush=True)
        # The last epoch: exit. The reply may or may not arrive before the socket closes.
        rec.log("c2s", "requests", RUN_COMMAND, "exit")
        write_frame(s.req, RUN_COMMAND, b"exit")
        try:
            s.req.settimeout(3.0)
            rty, reply = read_frame(s.req)
            rec.log("s2c", "requests", rty, reply)
        except (EOFError, OSError):
            pass
        how = s.drain_events(timeout=5.0, until_eof=True)
        print(f"RECORD exit: {how}", flush=True)
        print(f"RECORD-DONE {rec.lines} frames", flush=True)
    if child:
        child.kill()
        child.wait()


if __name__ == "__main__":
    main()
