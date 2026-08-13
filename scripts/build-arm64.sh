#!/usr/bin/env bash
# Platform: AArch64 / ARM 64-bit (Raspberry Pi 3/4/5, Apple M-series, AWS Graviton)
# Compiler: aarch64-linux-gnu-gcc
# Install:  sudo apt-get install gcc-aarch64-linux-gnu
# Output:   forever-text-arm64
# Run with: qemu-aarch64-static ./forever-text-arm64  (on Linux host)

set -e
cd "$(dirname "$0")/.."

aarch64-linux-gnu-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-arm64 \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-arm64"
