#!/usr/bin/env python3
# Copyright 2026 The Agency Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Self-contained runnable proof for the niri EventStream framing.

This is the evidence the C++ parser (niri_ipc_client.cc) is authored against the
REAL wire protocol, not a guessed one. It connects to $NIRI_SOCKET, sends the
bare JSON string "EventStream", asserts the {"Ok": ...} handshake, and asserts
that every subsequent line is a single-key JSON object whose key is one of the
11 tags the parser dispatches. stdlib only; no third-party deps.

Exit 0 and print PASS on success; exit 1 and print FAIL otherwise.
Run:  python3 src/agency/signal/niri/proof/niri_proto_probe.py
"""

import json
import os
import socket
import sys
import time

# The 11 event tags NiriIpcClient::HandleEventLine dispatches. MUST stay in sync
# with agency::niri::EventTag / WireKeyToEventTag in niri_events.{h,cc}.
KNOWN_TAGS = {
    "WorkspacesChanged",
    "WindowsChanged",
    "WindowOpenedOrChanged",
    "WindowClosed",
    "WorkspaceActivated",
    "WindowFocusChanged",
    "WorkspaceUrgencyChanged",
    "KeyboardLayoutsChanged",
    "OverviewOpenedOrClosed",
    "ConfigLoaded",
    "CastsChanged",
}

# How long to observe the stream after the handshake before declaring PASS.
OBSERVE_SECONDS = 1.5


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


def main():
    sock_path = os.environ.get("NIRI_SOCKET")
    if not sock_path:
        fail("NIRI_SOCKET is not set (is niri running in this session?)")
    if not os.path.exists(sock_path):
        fail("NIRI_SOCKET path does not exist: %s" % sock_path)

    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(sock_path)
    except OSError as exc:
        fail("could not connect to %s: %s" % (sock_path, exc))

    # The one request that flips the socket into streaming mode. Newline
    # terminated, exactly as niri_ipc_client.cc sends it.
    sock.sendall(b'"EventStream"\n')
    stream = sock.makefile("rb")

    # --- assertion 1: the handshake is a {"Ok": ...} object ---
    raw = stream.readline()
    if not raw:
        fail("connection closed before any handshake line")
    try:
        handshake = json.loads(raw)
    except json.JSONDecodeError as exc:
        fail("handshake line was not valid JSON: %r (%s)" % (raw, exc))
    if not isinstance(handshake, dict) or "Ok" not in handshake:
        fail('handshake was not a {"Ok": ...} object: %r' % handshake)
    print("handshake OK: %s" % json.dumps(handshake))

    # --- assertion 2: every event line is a single-key object with a known tag
    seen = {}
    unknown = set()
    non_single_key = 0
    deadline = time.time() + OBSERVE_SECONDS
    sock.settimeout(0.5)
    lines = 0
    while time.time() < deadline:
        try:
            raw = stream.readline()
        except socket.timeout:
            # No more events buffered right now; the initial resync burst is the
            # part we assert against, so a quiet tail is fine.
            break
        if not raw:
            fail("stream closed unexpectedly mid-observation")
        try:
            obj = json.loads(raw)
        except json.JSONDecodeError as exc:
            fail("event line was not valid JSON: %r (%s)" % (raw, exc))
        if not isinstance(obj, dict):
            fail("event line was not a JSON object: %r" % obj)
        lines += 1
        if len(obj) != 1:
            non_single_key += 1
            fail("event object was not single-key (size=%d): %r"
                 % (len(obj), obj))
        key = next(iter(obj))
        if key not in KNOWN_TAGS:
            unknown.add(key)
        seen[key] = seen.get(key, 0) + 1

    stream.close()
    sock.close()

    if lines == 0:
        fail("no event lines observed after handshake (expected at least the "
             "WorkspacesChanged/WindowsChanged resync)")
    if unknown:
        fail("observed event keys not in the known 11 tags: %s"
             % ", ".join(sorted(unknown)))
    if non_single_key:
        fail("%d event object(s) were not single-key" % non_single_key)

    print("observed %d event line(s); tags: %s"
          % (lines, ", ".join("%s=%d" % (k, v) for k, v in sorted(seen.items()))))
    print("all observed tags are in the known 11: %s"
          % ", ".join(sorted(KNOWN_TAGS)))
    print("PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
