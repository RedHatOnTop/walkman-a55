#!/bin/bash
# Build the picker UPG. See ../tools/build_upg.sh.
exec "$(dirname "$0")/../tools/build_upg.sh" "$(dirname "$0")"
