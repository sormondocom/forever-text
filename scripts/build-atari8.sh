#!/usr/bin/env bash
# Platform: Atari 400 / 800 / XL / XE (MOS 6502 CPU)
# Compiler: cl65 (cc65 compiler suite)
# Install:  sudo apt-get install cc65
# Output:   forever-text-atari8.xex  (Atari binary load format)
# Run with: Altirra emulator or real Atari 8-bit hardware + SIO2PC
# Note:     cc65/cl65 does not accept GCC flags; only -Os and -t are used.

set -e
cd "$(dirname "$0")/.."

cl65 -Os -t atari \
    -Isrc \
    -o forever-text-atari8.xex \
    src/main.c src/editor.c src/buffer.c src/platform/conio6502.c

echo "Built: forever-text-atari8.xex"
