#!/usr/bin/env bash
# Platform: macOS (Apple Silicon or Intel)
# Compiler: clang (Xcode Command Line Tools) or gcc via Homebrew
# Install:  xcode-select --install
# Output:   forever-text

set -e
cd "$(dirname "$0")/.."

cc -std=c89 -pedantic -Wall -Wextra -Werror -Os \
    -Isrc \
    -o forever-text \
    src/main.c src/editor.c src/buffer.c src/platform/ansi.c

echo "Built: forever-text"
