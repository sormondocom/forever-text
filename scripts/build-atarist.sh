#!/usr/bin/env bash
# Platform: Atari ST / TT / Falcon (68k CPU, MiNT POSIX-like OS)
# Compiler: m68k-atari-mint-gcc
# Install:  sudo add-apt-repository ppa:vriviere/ppa
#           sudo apt-get install gcc-m68k-atari-mint
# Output:   forever-text-atarist.tos
# Run with: Hatari emulator or real Atari ST hardware with MiNT installed
# Note:     MiNT supports termios and VT52/ANSI so we reuse ansi.c.

set -e
cd "$(dirname "$0")/.."

m68k-atari-mint-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-atarist.tos \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-atarist.tos"
