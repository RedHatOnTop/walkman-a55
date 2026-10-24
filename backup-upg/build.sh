#!/bin/bash
# Build the read-only backup UPG. See ../tools/build_upg.sh.
exec "$(dirname "$0")/../tools/build_upg.sh" "$(dirname "$0")"
