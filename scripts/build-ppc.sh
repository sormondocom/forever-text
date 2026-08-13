#!/usr/bin/env bash
# Platform: PowerPC (pre-Intel Macs, IBM RS/6000, BeOS)
# Compiler: powerpc-linux-gnu-gcc
# Install:  sudo apt-get install gcc-powerpc-linux-gnu
# Output:   forever-text-ppc
# Run with: qemu-ppc-static ./forever-text-ppc  (on Linux host)

set -e
cd "$(dirname "$0")/.."

powerpc-linux-gnu-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-ppc \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-ppc"
