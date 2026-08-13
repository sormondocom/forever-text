#!/usr/bin/env bash
# Platform: DOS 16-bit real mode (8086 / 8088)
# Compiler: ia16-elf-gcc (gcc-ia16)
# Install:  sudo add-apt-repository ppa:tkchia/gcc-ia16
#           sudo apt-get install gcc-ia16
# Output:   forever-text-dos16.exe
# Run with: DOSBox, DOSBox-X, or real DOS hardware

set -e
cd "$(dirname "$0")/.."

ia16-elf-gcc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -mcmodel=small \
    -Isrc \
    -o forever-text-dos16.exe \
    src/main.c src/editor.c src/buffer.c src/platform/bios.c \
    -li86

echo "Built: forever-text-dos16.exe  (16-bit DOS, BIOS platform)"
