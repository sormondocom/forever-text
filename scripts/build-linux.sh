#!/usr/bin/env bash
# Platform: Linux (x86-64 or any native GCC host)
# Compiler: gcc
# Install:  sudo apt-get install gcc make
# Output:   forever-text

set -e
cd "$(dirname "$0")/.."

gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c

echo "Built: forever-text"
