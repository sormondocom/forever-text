#!/usr/bin/env bash
# Platform: Apple IIe / IIc / IIGS (MOS 6502 / 65C02 CPU)
# Compiler: cl65 (cc65 compiler suite)
# Install:  sudo apt-get install cc65
# Output:   forever-text-apple2  (ProDOS binary)
# Run with: AppleWin, MAME, or real Apple II hardware + Disk II
# Note:     cc65/cl65 does not accept GCC flags; only -Os and -t are used.

set -e
cd "$(dirname "$0")/.."

cl65 -Os -t apple2enh \
    -Isrc \
    -o forever-text-apple2 \
    src/main.c src/editor.c src/buffer.c src/platform/conio6502.c

echo "Built: forever-text-apple2"
