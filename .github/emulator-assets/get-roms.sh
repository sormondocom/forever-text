#!/bin/sh
# Download emulator ROM images for CI screenshot testing.
#
# Usage: sh .github/emulator-assets/get-roms.sh [output-dir]
# Default output: ci-assets/emulator-roms/
#
# All downloads are best-effort (|| true).  Jobs that need these ROMs
# carry continue-on-error: true so a missing ROM produces a boot-screen
# screenshot rather than a red pipeline.
#
# Sources / legal status
# ----------------------
# atari8  : AltirraOS by Avery Lee — free replacement Atari OS ROM
# amiga   : AROS — GPL Amiga-compatible ROM (aros-development-team)
# trs80   : TRS-80 Model III ROM — Internet Archive (widely treated as abandonware)
# apple2  : Apple IIe ROM — Internet Archive (Apple has not pursued retrocomputing archives)
# ti99    : TI-99/4A ROM — released by TI for non-commercial/personal use

set -e

OUTDIR="${1:-ci-assets/emulator-roms}"
mkdir -p \
    "$OUTDIR/atari8" \
    "$OUTDIR/amiga" \
    "$OUTDIR/trs80" \
    "$OUTDIR/apple2" \
    "$OUTDIR/ti99"

# ---------------------------------------------------------------------------
# Atari 8-bit — AltirraOS (Avery Lee, free replacement for Atari OS ROMs)
# atari800 also supports -hle (built-in HLE; no file required) as a fallback.
# ---------------------------------------------------------------------------
echo "==> [atari8] AltirraOS..."
{
    curl -fsSL --retry 3 \
        "https://github.com/jhallen/joes-sandbox/raw/master/atari/altirraos/ALTIRRAOS-XL.ROM" \
        -o "$OUTDIR/atari8/ALTIRRAOS-XL.ROM"
    curl -fsSL --retry 3 \
        "https://github.com/jhallen/joes-sandbox/raw/master/atari/altirraos/ALTIRRAOS-XLBASIC.ROM" \
        -o "$OUTDIR/atari8/ALTIRRAOS-BASIC.ROM"
} || echo "  [atari8] download failed — will use -hle mode"

# ---------------------------------------------------------------------------
# Amiga — AROS Kickstart ROM (GPL, aros-development-team on GitHub)
# FS-UAE from apt may already bundle a ROM; check the installed path first.
# ---------------------------------------------------------------------------
echo "==> [amiga] AROS Kickstart ROM..."
{
    FSUAE_ROM=$(find /usr /opt -name "aros-amiga-m68k-rom*" 2>/dev/null | head -1)
    if [ -n "$FSUAE_ROM" ]; then
        cp "$FSUAE_ROM" "$OUTDIR/amiga/aros-kickstart.rom"
        echo "  [amiga] found FS-UAE bundled ROM at $FSUAE_ROM"
    else
        curl -fsSL --retry 3 \
            "https://github.com/aros-development-team/AROS/releases/download/nightly/aros-m68k-rom.zip" \
            -o /tmp/aros-rom.zip
        unzip -jo /tmp/aros-rom.zip -d "$OUTDIR/amiga/" 2>/dev/null
        ROM=$(find "$OUTDIR/amiga" -name "*.rom" | head -1)
        [ -n "$ROM" ] && mv "$ROM" "$OUTDIR/amiga/aros-kickstart.rom" 2>/dev/null || true
    fi
} || echo "  [amiga] download failed"

# ---------------------------------------------------------------------------
# TRS-80 Model III — ROM from Internet Archive
# ---------------------------------------------------------------------------
echo "==> [trs80] TRS-80 Model III ROM..."
{
    curl -fsSL --retry 3 \
        "https://archive.org/download/TRS-80-Model-3-ROMs/model3.rom" \
        -o "$OUTDIR/trs80/model3.rom"
} || {
    curl -fsSL --retry 3 \
        "https://archive.org/download/trs-80-coco3-roms/model3.rom" \
        -o "$OUTDIR/trs80/model3.rom"
} || echo "  [trs80] download failed"

# ---------------------------------------------------------------------------
# Apple IIe — ROM from Internet Archive
# linapple expects: apple2e.rom in ~/.linapple/ or the working directory
# ---------------------------------------------------------------------------
echo "==> [apple2] Apple IIe ROM..."
{
    curl -fsSL --retry 3 \
        "https://archive.org/download/Apple_II_BIOS/APPLE2E.ROM" \
        -o "$OUTDIR/apple2/apple2e.rom"
} || echo "  [apple2] download failed"

# ---------------------------------------------------------------------------
# TI-99/4A — ROM set (TI released for non-commercial use)
# MAME expects ti99_4a.zip in its rompath directory
# ---------------------------------------------------------------------------
echo "==> [ti99] TI-99/4A ROMs..."
{
    curl -fsSL --retry 3 \
        "https://archive.org/download/ti99_4a_romset/ti99_4a.zip" \
        -o "$OUTDIR/ti99/ti99_4a.zip"
} || echo "  [ti99] download failed"

echo ""
echo "ROM inventory:"
find "$OUTDIR" -type f 2>/dev/null | sort | sed 's|^|  |'
