#!/bin/sh
# Copyright 2026 The views-shell Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Test only: answers the first request (initialize, id "host-1") with
# protocol 2, then waits for end of input or a signal.
read -r _
printf '%s\n' '{"jsonrpc":"2.0","id":"host-1","result":{"protocol":2}}'
while read -r _; do :; done
