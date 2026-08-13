#!/usr/bin/env bash
# Platform: TRS-80 Model III (Zilog Z80 CPU, direct VRAM + ROM keyboard)
# Compiler: sdcc (Small Device C Compiler)
# Install:  sudo apt-get install sdcc binutils
# Output:   forever-text-trs80.cmd  (TRS-80 TRSDOS CMD binary)
# Run with: sdltrs, trs80gp, or real TRS-80 hardware
# Note:     SDCC requires a two-pass build — compile each translation unit
#           to a .rel file first, then link them together.

set -e
cd "$(dirname "$0")/.."

sdcc --std-c89 -mz80 -c -Isrc src/editor.c
sdcc --std-c89 -mz80 -c -Isrc src/buffer.c
sdcc --std-c89 -mz80 -c -Isrc src/platform/z80.c

sdcc --std-c89 -mz80 \
    -Isrc \
    --code-loc 0x5200 --data-loc 0x5C00 \
    --out-fmt-ihx \
    -o forever-text-trs80.ihx \
    src/main.c editor.rel buffer.rel z80.rel

objcopy -I ihex -O binary \
    forever-text-trs80.ihx forever-text-trs80.cmd

rm -f editor.rel buffer.rel z80.rel

echo "Built: forever-text-trs80.cmd  (TRS-80 Model III CMD binary)"
