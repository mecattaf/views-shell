#!/usr/bin/env bash
# autoninja -C out/<out> <targets...> in the Chromium checkout (inside the FHS environment).
set -euo pipefail
out="$1"; shift
exec "$HOME/views-bench/build-env" -c "cd ~/chromium/src && autoninja -C out/$out $*"
