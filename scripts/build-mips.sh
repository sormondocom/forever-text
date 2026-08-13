#!/usr/bin/env bash
# Platform: MIPS (SGI workstations, network appliances, many embedded systems)
# Compiler: mips-linux-gnu-gcc
# Install:  sudo apt-get install gcc-mips-linux-gnu
# Output:   forever-text-mips
# Run with: qemu-mips-static ./forever-text-mips  (on Linux host)

set -e
cd "$(dirname "$0")/.."

mips-linux-gnu-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-mips \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-mips"
