#!/usr/bin/env bash
# Run one command inside the FHS build environment on the bench.
exec "$HOME/views-bench/build-env" -c "$*"
