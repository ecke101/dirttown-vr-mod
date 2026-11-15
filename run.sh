#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
exec ./.local/vr/bin/dirttown-vr "$@"
