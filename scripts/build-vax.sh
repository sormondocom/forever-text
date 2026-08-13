#!/usr/bin/env bash
# Platform: DEC VAX (running NetBSD/VAX, OpenBSD/VAX, or VMS + GCC)
# Compiler: vax-linux-gnu-gcc
# Install:  sudo apt-get install gcc-vax-linux-gnu
# Output:   forever-text-vax
# Run with: SIMH VAX emulator, or native VAX hardware via serial terminal

set -e
cd "$(dirname "$0")/.."

vax-linux-gnu-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-vax \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-vax"
