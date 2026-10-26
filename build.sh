#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
xcrun clang -O2 -Wall -Wextra -Werror -arch arm64 -mmacosx-version-min=12.0 \
    -o build/hilink-switch hilink-switch.c \
    -framework IOKit -framework CoreFoundation -framework DiskArbitration
codesign --force --sign - build/hilink-switch
file build/hilink-switch
