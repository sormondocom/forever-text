#!/usr/bin/env bash
# Platform: RISC-V 64-bit (modern open ISA, SiFive boards, VisionFive 2, etc.)
# Compiler: riscv64-linux-gnu-gcc
# Install:  sudo apt-get install gcc-riscv64-linux-gnu
# Output:   forever-text-riscv64
# Run with: qemu-riscv64-static ./forever-text-riscv64  (on Linux host)

set -e
cd "$(dirname "$0")/.."

riscv64-linux-gnu-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text-riscv64 \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c \
    -static

echo "Built: forever-text-riscv64"
