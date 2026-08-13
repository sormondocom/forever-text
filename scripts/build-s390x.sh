#!/usr/bin/env bash
# Platform: IBM Z / S390x (direct descendant of the S/360 architecture, running Linux)
# Compiler: s390x-linux-gnu-gcc
# Install:  sudo apt-get install gcc-s390x-linux-gnu
# Output:   forever-text-s390x
# Run with: qemu-s390x-static ./forever-text-s390x  (on Linux host)

set -e
cd "$(dirname "$0")/.."

s390x-linux-gnu-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-s390x \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-s390x"
