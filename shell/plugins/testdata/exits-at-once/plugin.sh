#!/bin/sh
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test only: says why on stderr and exits 3 before reading initialize, so the
# host sees a crash on every launch.
echo "test.exits-at-once: exiting with status 3" >&2
exit 3
