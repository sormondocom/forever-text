#!/usr/bin/env bash
# Platform: TI-99/4A (Texas Instruments TMS9900 16-bit CPU)
# Compiler: tms9900-gcc (community GCC backend for TMS9900)
# Install:  Build from source: https://github.com/jedimatt42/tms9900-gcc
#           (not in standard package repositories)
# Output:   forever-text-ti99.out  (TIFILES/V9T9 format)
# Run with: Classic99, MAME (ti99_4a driver), or js99er.net

set -e
cd "$(dirname "$0")/.."

tms9900-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-ti99.out \
    src/main.c src/editor.c src/buffer.c src/platform/ti99.c

echo "Built: forever-text-ti99.out  (TI-99/4A TMS9900 binary)"
