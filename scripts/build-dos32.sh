#!/usr/bin/env bash
# Platform: DOS 32-bit protected mode (DJGPP + CWSDPMI extender)
# Compiler: i586-pc-msdosdjgpp-gcc (DJGPP cross-compiler)
# Install:  sudo apt-get install gcc-i686-pc-msdosdjgpp binutils-i686-pc-msdosdjgpp
#           (or build from https://github.com/andrewwutw/build-djgpp)
# Output:   forever-text-dos32.exe
# Run with: DOSBox, DOSBox-X, or real DOS with CWSDPMI.EXE
# Note:     ANSI.SYS or an ANSI driver must be loaded in CONFIG.SYS.
#           Uses gnu89 because DJGPP system headers require GNU extensions.

set -e
cd "$(dirname "$0")/.."

i586-pc-msdosdjgpp-gcc -std=gnu89 -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-dos32.exe \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-dos32.exe  (32-bit DOS, DJGPP)"
