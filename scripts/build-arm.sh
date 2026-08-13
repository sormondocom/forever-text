#!/usr/bin/env bash
# Platform: ARM 32-bit (Raspberry Pi 1/2/3 in 32-bit mode, embedded Linux boards)
# Compiler: arm-linux-gnueabi-gcc
# Install:  sudo apt-get install gcc-arm-linux-gnueabi
# Output:   forever-text-arm
# Run with: qemu-arm-static ./forever-text-arm  (on Linux host)

set -e
cd "$(dirname "$0")/.."

arm-linux-gnueabi-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-arm \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-arm"
