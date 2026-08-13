#!/usr/bin/env bash
# Platform: SPARC (Sun workstations running SunOS/Solaris, System V)
# Compiler: sparc-linux-gnu-gcc
# Install:  sudo apt-get install gcc-sparc-linux-gnu
# Output:   forever-text-sparc
# Run with: qemu-sparc-static ./forever-text-sparc  (on Linux host)

set -e
cd "$(dirname "$0")/.."

sparc-linux-gnu-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-sparc \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-sparc"
