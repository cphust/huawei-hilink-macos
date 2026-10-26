#!/bin/sh
set -eu
cd "$(dirname "$0")"
exec ./build/hilink-switch --status
