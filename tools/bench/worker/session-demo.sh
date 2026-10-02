#!/usr/bin/env bash
# The harness client of an assembled views-shell run: a short scripted session
# against the shell, as a user would drive it.
#   session-demo.sh <views_shell> <views_shell flags...>
# Runs inside the FHS environment, as the client command of worker/headless.sh
# (so under runtime-test, with the private headless scroll's WAYLAND_DISPLAY and
# SCROLLSOCK, and headless.sh's private session bus). It:
#   1. starts views_shell with the given flags in the background, its output
#      going both to this script's stdout (headless.sh's client.log) and to a
#      private copy this script reads;
#   2. waits for the first wl_surface.attach in that output (WAYLAND_DEBUG=1
#      comes from headless.sh);
#   3. waits until the shell owns org.freedesktop.Notifications, then runs
#      notify-send -a demo -t 8000 'views-shell' 'hello from the bench';
#   4. with --demo-keyboard, waits for the modal's layer surface, then types
#      'hello' with wtype (zwp_virtual_keyboard_v1); wtype first sleeps 1.5 s
#      with its virtual keyboard created, so the seat has a keyboard and the
#      client has bound wl_keyboard before the first key;
#   5. waits for views_shell and prints its exit code ("session-demo: views_shell
#      exited rc N"; headless.sh normally kills it first).
# Every step prints one "session-demo: ..." line. The EXIT trap kills the shell.
set -uo pipefail
[ "$#" -ge 1 ] || { echo 'usage: session-demo.sh <views_shell> <flags...>' >&2; exit 2; }

keyboard=0
for a in "$@"; do [ "$a" = --demo-keyboard ] && keyboard=1; done

LOG="$(mktemp "${XDG_RUNTIME_DIR:-/tmp}/session-demo.XXXXXX")"
SHELL_PID=""
cleanup() {
  [ -n "$SHELL_PID" ] && kill "$SHELL_PID" 2>/dev/null && wait "$SHELL_PID" 2>/dev/null
  rm -f "$LOG"
}
trap cleanup EXIT
trap 'exit 143' TERM INT HUP

# wait_for <regex> <seconds>: 0 once the shell's output matches, 1 on timeout
# or when the shell is gone.
wait_for() {
  local i
  for i in $(seq 1 $(( $2 * 20 ))); do
    grep -qE "$1" "$LOG" && return 0
    kill -0 "$SHELL_PID" 2>/dev/null || return 1
    sleep 0.05
  done
  return 1
}

"$@" > >(tee -a "$LOG") 2>&1 &
SHELL_PID=$!
echo "session-demo: started $1 pid $SHELL_PID"

if wait_for 'wl_surface[#@][0-9]+\.attach' 30; then
  echo "session-demo: first attach seen"
else
  echo "session-demo: no attach within 30 s"; exit 1
fi

if wait_for 'notifications: (serving|org.freedesktop.Notifications not owned|no session bus)' 10 &&
   grep -q 'notifications: serving' "$LOG"; then
  notify-send -a demo -t 8000 'views-shell' 'hello from the bench'
  echo "session-demo: notify-send rc $?"
else
  echo "session-demo: the shell does not serve notifications; notify-send skipped"
fi

if [ "$keyboard" = 1 ]; then
  if wait_for 'get_layer_surface\(.*"views-shell-modal"' 10; then
    echo "session-demo: modal surface requested"
    wtype -s 1500 hello
    echo "session-demo: wtype rc $?"
  else
    echo "session-demo: no modal surface; wtype skipped"
  fi
fi

wait "$SHELL_PID"
rc=$?
SHELL_PID=""
echo "session-demo: views_shell exited rc $rc"
exit "$rc"
