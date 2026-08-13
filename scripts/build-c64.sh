#!/usr/bin/env bash
# Platform: Commodore 64 (MOS 6510 / 6502 compatible)
# Compiler: cl65 (cc65 compiler suite)
# Install:  sudo apt-get install cc65
# Output:   forever-text-c64.prg
# Run with: VICE (x64sc), C64 Mini, or real C64 hardware + 1541 disk drive
# Note:     cc65/cl65 does not accept GCC flags; only -Os and -t are used.

set -e
cd "$(dirname "$0")/.."

cl65 -Os -t c64 \
    -Isrc \
    -o forever-text-c64.prg \
    src/main.c src/editor.c src/buffer.c src/platform/conio6502.c

echo "Built: forever-text-c64.prg"
