#!/usr/bin/env bash
# Platform: Motorola 68000 (old Macs, Sun-3, NeXT, generic Linux/m68k)
# Compiler: m68k-linux-gnu-gcc
# Install:  sudo apt-get install gcc-m68k-linux-gnu
# Output:   forever-text-m68k
# Run with: qemu-m68k-static ./forever-text-m68k  (on Linux host)

set -e
cd "$(dirname "$0")/.."

m68k-linux-gnu-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-m68k \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-m68k"
