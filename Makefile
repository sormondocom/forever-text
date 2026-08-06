# Makefile for Forever Text
#
# Native build:
#   make              (auto-detects host OS)
#   make test         compile and run buffer unit tests natively
#
# Cross-compilation targets:
#   make dos16        16-bit real-mode DOS  (ia16-elf-gcc)
#   make dos32        32-bit DOS extender   (DJGPP: i586-pc-msdosdjgpp-gcc)
#   make vax          VAX/VMS or VAX/NetBSD (vax-linux-gnu-gcc)
#   make m68k         Motorola 68k          (m68k-linux-gnu-gcc)
#   make sparc        SPARC                 (sparc-linux-gnu-gcc)
#   make ppc          PowerPC               (powerpc-linux-gnu-gcc)
#   make mips         MIPS                  (mips-linux-gnu-gcc)
#   make s390x        IBM Z (S/360 lineage) (s390x-linux-gnu-gcc)
#   make riscv        RISC-V 64-bit         (riscv64-linux-gnu-gcc)
#
# Cross-test targets (compile test_buffer for arch, run under QEMU user-mode):
#   make test-m68k    make test-sparc    make test-ppc
#   make test-mips    make test-s390x    make test-riscv
#   make test-arm     make test-arm64
#
# All cross targets produce static binaries where possible so the
# result is self-contained and does not require shared libraries on
# the target system.

# ------------------------------------------------------------------ #
# Compiler flags shared across all targets                            #
# ------------------------------------------------------------------ #

# Strict C89 everywhere.  -pedantic promotes extensions to errors so
# we stay clean on every compiler we test against.
CSTD    = -std=c89 -pedantic

# Warnings as errors keeps the codebase clean across all targets.
WARN    = -Wall -Wextra -Werror

# Optimise for size rather than speed — important for old machines
# with limited RAM.  Override with OPT=-O2 if you prefer speed.
OPT     ?= -Os

CFLAGS  = $(CSTD) $(WARN) $(OPT)

SRCS    = src/main.c \
          src/editor.c \
          src/buffer.c

INCDIRS = -Isrc

# ------------------------------------------------------------------ #
# Host detection                                                       #
# ------------------------------------------------------------------ #

ifeq ($(OS),Windows_NT)
    HOST := windows
    OUT_EXT := .exe
else
    HOST := unix
    OUT_EXT :=
endif

# ------------------------------------------------------------------ #
# Native build                                                         #
# ------------------------------------------------------------------ #

ifeq ($(HOST),windows)
    # Native Windows: use the Win32 Console platform layer
    PLATFORM_SRC := src/platform/win32.c
    CC_NATIVE    := gcc
    LDFLAGS_NATIVE :=
else
    # Unix/Linux/macOS/VAX-Linux: use the ANSI/VT100 platform layer
    PLATFORM_SRC := src/platform/ansi.c
    CC_NATIVE    := cc
    LDFLAGS_NATIVE :=
endif

TARGET_NATIVE         := forever-text$(OUT_EXT)
TARGET_TEST           := test-buffer$(OUT_EXT)
TARGET_PLATFORM_TEST  := test-platform$(OUT_EXT)
TEST_SRCS             := tests/test_buffer.c src/buffer.c
PLATFORM_TEST_SRCS    := tests/test_platform.c

.PHONY: all native test clean \
        dos16 dos32 vax m68k sparc ppc mips s390x riscv arm arm64 \
        c64 atari8 apple2 amiga atarist trs80 ti99 \
        test-m68k test-sparc test-ppc test-mips test-s390x test-riscv \
        test-arm test-arm64 \
        test-platform test-platform-headless \
        test-platform-m68k test-platform-sparc test-platform-ppc \
        test-platform-mips test-platform-s390x test-platform-riscv \
        test-platform-arm test-platform-arm64

all: native

native: $(TARGET_NATIVE)

$(TARGET_NATIVE): $(SRCS) $(PLATFORM_SRC)
	$(CC_NATIVE) $(CFLAGS) $(INCDIRS) -o $@ $(SRCS) $(PLATFORM_SRC) $(LDFLAGS_NATIVE)
	@echo "Built: $@"

# Build and run the buffer unit tests natively.
# The test binary has no platform layer dependency — pure buffer logic.
test: $(TARGET_TEST)
	./$(TARGET_TEST)

$(TARGET_TEST): $(TEST_SRCS)
	$(CC_NATIVE) $(CFLAGS) $(INCDIRS) -o $@ $(TEST_SRCS)
	@echo "Built: $@"

# Platform layer compliance tests.
#
# test-platform   — interactive mode: visual checks + key-code verification.
#                   Requires a real terminal; intended for developer use.
# test-platform-headless — automatic checks only: crash-safety and sanity.
#                   Safe for CI pipelines and QEMU user-mode emulation.
$(TARGET_PLATFORM_TEST): $(PLATFORM_TEST_SRCS) $(PLATFORM_SRC)
	$(CC_NATIVE) $(CFLAGS) $(INCDIRS) \
	    -o $@ $(PLATFORM_TEST_SRCS) $(PLATFORM_SRC) $(LDFLAGS_NATIVE)
	@echo "Built: $@"

test-platform: $(TARGET_PLATFORM_TEST)
	./$(TARGET_PLATFORM_TEST)

test-platform-headless: $(TARGET_PLATFORM_TEST)
	./$(TARGET_PLATFORM_TEST) headless

# ------------------------------------------------------------------ #
# Cross-compilation targets                                            #
# ------------------------------------------------------------------ #

# 16-bit real-mode DOS (ia16-elf-gcc from https://github.com/tkchia/gcc-ia16)
# Uses BIOS platform layer — no ANSI.SYS required.
dos16: $(SRCS) src/platform/bios.c
	ia16-elf-gcc $(CSTD) $(WARN) $(OPT) -mcmodel=small \
	    $(INCDIRS) -o forever-text-dos16.exe \
	    $(SRCS) src/platform/bios.c \
	    -li86
	@echo "Built: forever-text-dos16.exe  (16-bit DOS COM/EXE)"

# 32-bit DOS (DJGPP — protected mode, requires a DOS extender like CWSDPMI)
# Uses ANSI platform layer; load ANSI.SYS in CONFIG.SYS on the target.
dos32: $(SRCS) src/platform/ansi.c
	i586-pc-msdosdjgpp-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-dos32.exe \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-dos32.exe  (32-bit DOS, DJGPP)"

# VAX running NetBSD/VAX or a VT100-connected VMS shell with GCC
vax: $(SRCS) src/platform/ansi.c
	vax-linux-gnu-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-vax \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-vax"

# Motorola 68000 — old Macs (System 6/7), Amiga, Sun-3, NeXT
m68k: $(SRCS) src/platform/ansi.c
	m68k-linux-gnu-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-m68k \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-m68k"

# SPARC — Sun workstations running SunOS/Solaris, System V
sparc: $(SRCS) src/platform/ansi.c
	sparc-linux-gnu-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-sparc \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-sparc"

# PowerPC — pre-Intel Macs, IBM RS/6000, some game consoles
ppc: $(SRCS) src/platform/ansi.c
	powerpc-linux-gnu-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-ppc \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-ppc"

# MIPS — SGI workstations, many embedded and network systems
mips: $(SRCS) src/platform/ansi.c
	mips-linux-gnu-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-mips \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-mips"

# IBM Z — direct descendant of the S/360 architecture (running Linux)
s390x: $(SRCS) src/platform/ansi.c
	s390x-linux-gnu-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-s390x \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-s390x"

# RISC-V 64-bit — modern open ISA
riscv: $(SRCS) src/platform/ansi.c
	riscv64-linux-gnu-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-riscv64 \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-riscv64"

# ARM 32-bit — Raspberry Pi 1/2/3 (32-bit), most embedded ARM Linux boards
arm: $(SRCS) src/platform/ansi.c
	arm-linux-gnueabi-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-arm \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-arm"

# AArch64 / ARM 64-bit — Raspberry Pi 3/4/5 (64-bit), Apple M-series via Rosetta
arm64: $(SRCS) src/platform/ansi.c
	aarch64-linux-gnu-gcc $(CFLAGS) $(INCDIRS) \
	    -o forever-text-arm64 \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-arm64"

# ------------------------------------------------------------------ #
# 6502 targets via cc65                                                #
# cc65 uses cl65 as its compiler driver and selects the target        #
# platform with -t.  The conio6502.c platform layer uses cc65's       #
# conio.h which is implemented per target in the cc65 library.        #
# ------------------------------------------------------------------ #

# Commodore 64
c64: $(SRCS) src/platform/conio6502.c
	cl65 $(CSTD) $(WARN) $(OPT) -t c64 \
	    $(INCDIRS) -o forever-text-c64.prg \
	    $(SRCS) src/platform/conio6502.c
	@echo "Built: forever-text-c64.prg"

# Atari 400/800/XL/XE
atari8: $(SRCS) src/platform/conio6502.c
	cl65 $(CSTD) $(WARN) $(OPT) -t atari \
	    $(INCDIRS) -o forever-text-atari8.xex \
	    $(SRCS) src/platform/conio6502.c
	@echo "Built: forever-text-atari8.xex"

# Apple II (requires cc65 with apple2 target support)
apple2: $(SRCS) src/platform/conio6502.c
	cl65 $(CSTD) $(WARN) $(OPT) -t apple2enh \
	    $(INCDIRS) -o forever-text-apple2 \
	    $(SRCS) src/platform/conio6502.c
	@echo "Built: forever-text-apple2"

# ------------------------------------------------------------------ #
# Amiga 68k (amiga-gcc — bebbo's GCC cross-compiler)                  #
# Produces AmigaOS HUNK format executable.                            #
# Works in AmigaOS 2.0+ Shell (ANSI sequences supported natively).    #
# ------------------------------------------------------------------ #

amiga: $(SRCS) src/platform/amiga.c
	m68k-amigaos-gcc $(CSTD) $(WARN) $(OPT) \
	    $(INCDIRS) -o forever-text-amiga \
	    $(SRCS) src/platform/amiga.c \
	    -noixemul
	@echo "Built: forever-text-amiga  (AmigaOS HUNK executable)"

# ------------------------------------------------------------------ #
# Atari ST — 68k CPU, MiNT OS (m68k-atari-mint-gcc)                  #
# MiNT is a POSIX-like OS layer for Atari ST/TT/Falcon.              #
# Supports termios and ANSI terminals so we reuse ansi.c.             #
# ------------------------------------------------------------------ #

atarist: $(SRCS) src/platform/ansi.c
	m68k-atari-mint-gcc $(CSTD) $(WARN) $(OPT) \
	    $(INCDIRS) -o forever-text-atarist.tos \
	    $(SRCS) src/platform/ansi.c \
	    -static
	@echo "Built: forever-text-atarist.tos  (Atari ST MiNT executable)"

# ------------------------------------------------------------------ #
# TRS-80 Model III — Zilog Z80, direct hardware I/O (SDCC)            #
# ------------------------------------------------------------------ #

trs80: $(SRCS) src/platform/z80.c
	sdcc --std-c89 -mz80 \
	    $(INCDIRS) \
	    --code-loc 0x5200 --data-loc 0x5C00 \
	    -o forever-text-trs80.ihx \
	    src/main.c src/editor.c src/buffer.c src/platform/z80.c
	objcopy -I ihex -O binary \
	    forever-text-trs80.ihx forever-text-trs80.cmd
	@echo "Built: forever-text-trs80.cmd  (TRS-80 Model III CMD binary)"

# ------------------------------------------------------------------ #
# TI-99/4A (TMS9900) via gcc-tms9900                                  #
#                                                                      #
# The TMS9900 is a 16-bit CPU unique to Texas Instruments.            #
# gcc-tms9900 is a GCC backend for this architecture maintained by    #
# the TI-99 community.  It is not in standard package repositories;   #
# see https://github.com/jedimatt42/tms9900-gcc for build instructions#
# or https://atariage.com/forums/ for prebuilt binaries.              #
#                                                                      #
# The binary is produced as a TIFILES/V9T9 disk image loadable in    #
# the Classic99, MAME, or js99er emulators.                           #
# ------------------------------------------------------------------ #

ti99: $(SRCS) src/platform/ti99.c
	tms9900-gcc $(CSTD) $(WARN) $(OPT) \
	    $(INCDIRS) \
	    -o forever-text-ti99.out \
	    $(SRCS) src/platform/ti99.c
	@echo "Built: forever-text-ti99.out  (TI-99/4A TMS9900 binary)"

# ------------------------------------------------------------------ #
# Cross-test targets                                                   #
# Compile test_buffer for the target architecture, then run it under  #
# QEMU user-mode emulation.  Requires qemu-user-static on the host.   #
# ------------------------------------------------------------------ #

test-m68k:
	m68k-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-buffer-m68k $(TEST_SRCS)
	qemu-m68k-static ./test-buffer-m68k

test-sparc:
	sparc-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-buffer-sparc $(TEST_SRCS)
	qemu-sparc-static ./test-buffer-sparc

test-ppc:
	powerpc-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-buffer-ppc $(TEST_SRCS)
	qemu-ppc-static ./test-buffer-ppc

test-mips:
	mips-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-buffer-mips $(TEST_SRCS)
	qemu-mips-static ./test-buffer-mips

test-s390x:
	s390x-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-buffer-s390x $(TEST_SRCS)
	qemu-s390x-static ./test-buffer-s390x

test-riscv:
	riscv64-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-buffer-riscv64 $(TEST_SRCS)
	qemu-riscv64-static ./test-buffer-riscv64

test-arm:
	arm-linux-gnueabi-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-buffer-arm $(TEST_SRCS)
	qemu-arm-static ./test-buffer-arm

test-arm64:
	aarch64-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-buffer-arm64 $(TEST_SRCS)
	qemu-aarch64-static ./test-buffer-arm64

# ------------------------------------------------------------------ #
# Cross-compiled platform tests (headless, run under QEMU user-mode)  #
#                                                                      #
# These verify that the ANSI/VT100 platform layer compiles correctly   #
# for each target architecture AND that all platform functions execute  #
# without crashing when run inside QEMU user-mode emulation.           #
# Interactive / visual checks require a real terminal on the target    #
# hardware; they are not included here.                                 #
# ------------------------------------------------------------------ #

test-platform-m68k:
	m68k-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-platform-m68k \
	    $(PLATFORM_TEST_SRCS) src/platform/ansi.c
	qemu-m68k-static ./test-platform-m68k headless

test-platform-sparc:
	sparc-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-platform-sparc \
	    $(PLATFORM_TEST_SRCS) src/platform/ansi.c
	qemu-sparc-static ./test-platform-sparc headless

test-platform-ppc:
	powerpc-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-platform-ppc \
	    $(PLATFORM_TEST_SRCS) src/platform/ansi.c
	qemu-ppc-static ./test-platform-ppc headless

test-platform-mips:
	mips-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-platform-mips \
	    $(PLATFORM_TEST_SRCS) src/platform/ansi.c
	qemu-mips-static ./test-platform-mips headless

test-platform-s390x:
	s390x-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-platform-s390x \
	    $(PLATFORM_TEST_SRCS) src/platform/ansi.c
	qemu-s390x-static ./test-platform-s390x headless

test-platform-riscv:
	riscv64-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-platform-riscv64 \
	    $(PLATFORM_TEST_SRCS) src/platform/ansi.c
	qemu-riscv64-static ./test-platform-riscv64 headless

test-platform-arm:
	arm-linux-gnueabi-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-platform-arm \
	    $(PLATFORM_TEST_SRCS) src/platform/ansi.c
	qemu-arm-static ./test-platform-arm headless

test-platform-arm64:
	aarch64-linux-gnu-gcc $(CFLAGS) $(INCDIRS) -static \
	    -o test-platform-arm64 \
	    $(PLATFORM_TEST_SRCS) src/platform/ansi.c
	qemu-aarch64-static ./test-platform-arm64 headless

# ------------------------------------------------------------------ #
# Housekeeping                                                         #
# ------------------------------------------------------------------ #

clean:
	rm -f forever-text forever-text.exe test-buffer test-buffer.exe \
	      test-platform test-platform.exe \
	      forever-text-dos16.exe forever-text-dos32.exe \
	      forever-text-vax forever-text-m68k forever-text-sparc \
	      forever-text-ppc forever-text-mips forever-text-s390x \
	      forever-text-riscv64 forever-text-arm forever-text-arm64 \
	      forever-text-c64.prg forever-text-atari8.xex forever-text-apple2 \
	      forever-text-amiga forever-text-atarist.tos \
	      forever-text-trs80.ihx forever-text-trs80.cmd \
	      forever-text-ti99.out \
	      test-buffer-m68k test-buffer-sparc test-buffer-ppc \
	      test-buffer-mips test-buffer-s390x test-buffer-riscv64 \
	      test-buffer-arm test-buffer-arm64 \
	      test-platform-m68k test-platform-sparc test-platform-ppc \
	      test-platform-mips test-platform-s390x test-platform-riscv64 \
	      test-platform-arm test-platform-arm64 \
	      ft_test_tmp.txt
