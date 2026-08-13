#!/usr/bin/env bash
# Platform: Commodore Amiga (AmigaOS 2.0+, 68000/68020/68040)
# Compiler: m68k-amigaos-gcc (bebbo's GCC cross-compiler)
# Install:  Docker image:  docker pull amigadev/crosstools:m68k-amigaos
#           Native:        https://github.com/bebbo/amiga-gcc
# Output:   forever-text-amiga  (AmigaOS HUNK executable)
# Run with: FS-UAE, WinUAE, or real Amiga hardware
# Note:     -noixemul links against amiga.lib instead of ixemul.library so
#           the binary runs without third-party libraries on the target.

set -e
cd "$(dirname "$0")/.."

m68k-amigaos-gcc -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-amiga \
    src/main.c src/editor.c src/buffer.c src/platform/amiga.c \
    -noixemul

echo "Built: forever-text-amiga  (AmigaOS HUNK executable)"
