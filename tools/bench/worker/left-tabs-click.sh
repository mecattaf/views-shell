#!/usr/bin/env bash
# The harness client of a --left-tabs run on the compositor model: a click on
# a tab, as a user would make it, through the headless scroll's seat.
#   left-tabs-click.sh <tab index, 0-based> <views_shell> <views_shell flags...>
# Runs inside the FHS environment as the client command of worker/headless.sh
# (so under runtime-test, with the private headless scroll's WAYLAND_DISPLAY and
# SCROLLSOCK). It:
#   1. starts views_shell with the given flags in the background, its output
#      going both to this script's stdout (headless.sh's client.log) and to a
#      private copy this script reads;
#   2. waits for the strip to hold the model's workspaces ("left-tabs: N
#      workspaces from the compositor model");
#   3. moves the seat's cursor onto tab <index> (the rail is at x 0, the tabs
#      are 30 px rows with a 2 px gap under 12 px of padding, docs/tabs.md) and
#      presses and releases button 1 through `scrollmsg seat - cursor ...`;
#   4. waits for the request ("left-tabs: FocusWorkspace") and the echo (the
#      next "left-tabs: ... active" line), and prints which workspace the
#      compositor reports focused ("left-tabs-click: focused <name>");
#   5. waits for views_shell and prints its exit code.
# Every step prints one "left-tabs-click: ..." line. The EXIT trap kills the shell.
set -uo pipefail
[ "$#" -ge 2 ] || { echo 'usage: left-tabs-click.sh <index> <views_shell> <flags...>' >&2; exit 2; }
index="$1"; shift
SCROLLMSG="${SCROLLMSG:-$HOME/views-bench/scroll/bin/scrollmsg}"

LOG="$(mktemp "${XDG_RUNTIME_DIR:-/tmp}/left-tabs-click.XXXXXX")"
SHELL_PID=""
cleanup() {
  [ -n "$SHELL_PID" ] && kill "$SHELL_PID" 2>/dev/null && wait "$SHELL_PID" 2>/dev/null
  rm -f "$LOG"
}
trap cleanup EXIT
trap 'exit 143' TERM INT HUP

wait_for() {
  local i
  for i in $(seq 1 $(( $2 * 20 ))); do
    [ "$(grep -cE "$1" "$LOG")" -ge "${3:-1}" ] && return 0
    kill -0 "$SHELL_PID" 2>/dev/null || return 1
    sleep 0.05
  done
  return 1
}

"$@" > >(tee -a "$LOG") 2>&1 &
SHELL_PID=$!
echo "left-tabs-click: started $1 pid $SHELL_PID"

if wait_for 'left-tabs: [0-9]+ workspaces from the compositor model' 30; then
  echo "left-tabs-click: strip holds $(grep -oE 'left-tabs: [0-9]+ workspaces from the compositor model, active .*' "$LOG" | tail -1 | sed 's/left-tabs: //')"
else
  echo "left-tabs-click: the strip never held the model's workspaces"; exit 1
fi
sleep 1

x=60; y=$(( 12 + index * 32 + 15 ))
"$SCROLLMSG" "seat - cursor set $x $y" >/dev/null && echo "left-tabs-click: cursor at $x,$y (tab $index)"
"$SCROLLMSG" "seat - cursor press button1" >/dev/null
sleep 0.1
"$SCROLLMSG" "seat - cursor release button1" >/dev/null && echo "left-tabs-click: button1 pressed and released"

if wait_for 'left-tabs: FocusWorkspace ' 5; then
  echo "left-tabs-click: request $(grep -oE 'left-tabs: FocusWorkspace .*' "$LOG" | tail -1 | sed 's/left-tabs: //')"
else
  echo "left-tabs-click: no FocusWorkspace request within 5 s"; exit 1
fi
if wait_for 'left-tabs: [0-9]+ workspaces from the compositor model' 5 2; then
  echo "left-tabs-click: focused $(grep -oE 'active .*' "$LOG" | tail -1 | sed 's/active //')"
else
  echo "left-tabs-click: no echo within 5 s"; exit 1
fi

wait "$SHELL_PID"
rc=$?
SHELL_PID=""
echo "left-tabs-click: views_shell exited rc $rc"
exit "$rc"
