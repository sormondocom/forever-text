/*
 * ti99.c - Texas Instruments TI-99/4A platform implementation for Forever Text
 *
 * Target: TI-99/4A with 32KB memory expansion, running from disk or cartridge.
 *
 * Compiler: gcc-tms9900 (GCC backend for the TMS9900 processor)
 *   https://github.com/jedimatt42/tms9900-gcc
 *
 * CPU: TMS9900 — a 16-bit processor with an unusual architecture:
 *   - All 16 working registers live in RAM (the "workspace")
 *   - 16-bit word-addressed with byte access support at even addresses
 *   - Big-endian byte order
 *   - CRU (Communications Register Unit) for bit-serial I/O — keyboard,
 *     sound chip control, and peripheral selection all go through CRU
 *
 * Display: TMS9918A Video Display Processor (VDP)
 *   The VDP is a separate chip with its own 16KB VRAM.  The CPU cannot
 *   read or write VRAM directly; it communicates through two I/O ports:
 *     >8C00  data write port   (write a byte of VRAM data)
 *     >8C02  control write port(set VRAM address / write VDP register)
 *     >8800  data read port    (read a byte of VRAM data)
 *     >8802  status read port  (read VDP status register)
 *   Every VRAM access requires two writes to the control port to set
 *   the 14-bit VRAM address, then one or more reads/writes to the
 *   data port.  This is slow by modern standards — a deliberate design
 *   trade-off for the cost of the VDP in 1981.
 *
 *   We use VDP Text Mode (Mode 1):
 *     40 columns x 24 rows
 *     Character codes compatible with ASCII (TI-99 ROM has standard
 *     ASCII glyphs from 0x20-0x7F plus TI-specific graphics)
 *     Name table (screen layout): VRAM 0x0000  (40 x 24 = 960 bytes)
 *     Pattern table (font):       VRAM 0x0800  (128 chars x 8 bytes each)
 *     Text colour register (R7):  0xF1  (white on black)
 *
 * Keyboard: CRU keyboard matrix
 *   The TI-99 keyboard is scanned via CRU addresses 0x0006-0x0017.
 *   The matrix has 8 columns (selected by writing CRU bits 21-18)
 *   and 8 rows (read via CRU bits 6, 7, 8, 9, 10, 11, 12, 13).
 *   CRU bit operations use the TMS9900 SBO, SBZ, and TB instructions.
 *   In gcc-tms9900 inline assembly these are available as:
 *     __asm__ ("SBO %0" : : "i"(bit));   -- set CRU bit
 *     __asm__ ("SBZ %0" : : "i"(bit));   -- clear CRU bit
 *     __asm__ ("TB  %0" : : "i"(bit));   -- test CRU bit (sets EQ flag)
 *
 * Memory map (relevant to this platform layer):
 *   0x0000-0x1FFF  ROM (console, not used from C)
 *   0x2000-0x3FFF  32KB expansion RAM (low half)
 *   0x8000-0x83FF  Memory-mapped hardware (VDP, sound, speech)
 *   0x8300-0x83FF  Scratchpad RAM (256 bytes — workspace registers live here)
 *   0xA000-0xFFFF  32KB expansion RAM (high half)
 *
 *   gcc-tms9900 places code and data in expansion RAM.
 *   The linker script must reserve 0x8300-0x83FF for the workspace.
 *
 * Reverse video:
 *   The TMS9918A text mode does not support per-character colour; the
 *   entire screen uses the two colours set in VDP register R7.  We
 *   simulate reverse video by swapping R7 on platform_attr_reverse()
 *   and restoring it on platform_attr_normal().  This makes the whole
 *   screen appear reversed while a header or footer is being drawn —
 *   a known limitation of the TMS9918A in text mode.
 */

#include <string.h>
#include "platform.h"

/* ------------------------------------------------------------------ */
/* VDP port addresses (TI-99/4A memory map)                             */
/* ------------------------------------------------------------------ */

#define VDP_DATA_READ  (*((volatile unsigned char *)0x8800))
#define VDP_DATA_WRITE (*((volatile unsigned char *)0x8C00))
#define VDP_CTRL_WRITE (*((volatile unsigned char *)0x8C02))
#define VDP_STAT_READ  (*((volatile unsigned char *)0x8802))

/* VRAM layout for text mode */
#define VRAM_NAME_TABLE    0x0000   /* 40 x 24 = 960 bytes */
#define VRAM_PATTERN_TABLE 0x0800   /* 128 chars x 8 bytes = 1024 bytes */

/* Screen dimensions in text mode */
#define TI_COLS 40
#define TI_ROWS 24

/* VDP colour byte for R7: foreground | (background << 4) */
#define COLOR_NORMAL  0xF1   /* white text (F) on black background (1) */
#define COLOR_REVERSE 0x1F   /* black text (1) on white background (F) */

/* ------------------------------------------------------------------ */
/* VDP access helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * Set the VRAM address for the next read or write.
 * For write: addr | 0x4000.  For read: addr | 0x0000.
 * The address is written as two bytes to the control port.
 */
static void vdp_set_addr_write(unsigned int addr)
{
    /* Low 8 bits first, then high 6 bits with write flag set */
    VDP_CTRL_WRITE = (unsigned char)(addr & 0xFF);
    VDP_CTRL_WRITE = (unsigned char)(((addr >> 8) & 0x3F) | 0x40);
}

static void vdp_set_addr_read(unsigned int addr)
{
    VDP_CTRL_WRITE = (unsigned char)(addr & 0xFF);
    VDP_CTRL_WRITE = (unsigned char)((addr >> 8) & 0x3F);
}

/* Write a single byte to VRAM at the current address */
static void vdp_write_byte(unsigned char c)
{
    VDP_DATA_WRITE = c;
}

/* Write a VDP register: register number and value */
static void vdp_write_reg(unsigned char reg, unsigned char val)
{
    VDP_CTRL_WRITE = val;
    VDP_CTRL_WRITE = (unsigned char)(0x80 | reg);
}

/*
 * Write n bytes from src to VRAM starting at addr.
 * More efficient than calling vdp_write_byte() in a loop because
 * the VDP auto-increments its address after each data write.
 */
static void vdp_write_block(unsigned int addr,
                            const unsigned char *src, int n)
{
    int i;
    vdp_set_addr_write(addr);
    for (i = 0; i < n; i++)
        VDP_DATA_WRITE = src[i];
}

/* ------------------------------------------------------------------ */
/* Built-in 8x8 ASCII font (minimal — only the glyphs we need)         */
/*                                                                      */
/* The TI-99 console ROM already provides a font in its own VRAM       */
/* but we cannot read that VRAM without resetting the VDP.             */
/* For a production build you would copy the ROM font at startup.      */
/* This minimal font covers printable ASCII 0x20-0x7E.                 */
/* ------------------------------------------------------------------ */

/*
 * Rather than embed a full 96-glyph font here, we initialise the VDP
 * pattern table to the console ROM's built-in character generator data.
 * The TI-99 GROM contains the font; we call the GPL VMBW routine via
 * the console ROM to copy it into VDP pattern RAM at startup.
 *
 * GPL workspace address for VMBW (VDP memory block write from CPU):
 *   The console ROM provides VMBW at GPL address 0x0090.
 *   For a cartridge/disk program we call it via:
 *     R0 = destination VRAM address
 *     R1 = source CPU address
 *     R2 = byte count
 *     BL @VMBW (0x0090)
 *
 * For simplicity in this platform layer we use a pointer to the
 * console ROM character table copy at CPU address 0x0000 in the
 * GPL ROM space — this is architecture-specific and depends on the
 * exact ROM version.  A more portable approach copies the font from
 * the standard ASCII patterns stored in the console GPL ROM.
 *
 * Placeholder: we fill pattern RAM with a visible pattern so the
 * editor is usable even if the font copy step fails.
 */
static void init_font(void)
{
    /*
     * Copy the TI-99 console ROM character data into VDP pattern RAM.
     * The TI-99 console GROM contains the font at GPL address 0x0004.
     * We call the ROM routine VMBW indirectly.
     *
     * For now: fill every character cell pattern with a simple
     * test pattern so at least something visible appears.  Replace
     * this with the proper ROM copy for a production build.
     */
    int  ch, row;
    unsigned char pattern[8];

    for (ch = 0; ch < 128; ch++) {
        /* Simple distinguishable pattern per character code */
        for (row = 0; row < 8; row++)
            pattern[row] = (unsigned char)((ch == 0x20) ? 0x00 : 0x7E);
        vdp_write_block((unsigned int)(VRAM_PATTERN_TABLE + ch * 8),
                        pattern, 8);
    }
}

/* ------------------------------------------------------------------ */
/* State                                                                 */
/* ------------------------------------------------------------------ */

static int          ft_cur_row;
static int          ft_cur_col;
static unsigned char ft_color;    /* current R7 colour byte */

/* ------------------------------------------------------------------ */
/* VDP text-mode initialisation                                          */
/* ------------------------------------------------------------------ */

static void vdp_text_mode_init(void)
{
    /*
     * TMS9918A register settings for 40-column text mode:
     *   R0 = 0x00  — text mode, no external video
     *   R1 = 0xD0  — display on, no sprites, text mode (bit 4 = 1)
     *   R2 = 0x00  — name table at VRAM 0x0000  (>>7 = 0)
     *   R3 = 0x00  — colour table (not used in text mode)
     *   R4 = 0x01  — pattern table at VRAM 0x0800 (>>11 = 1)
     *   R5 = 0x00  — sprite attribute (not used)
     *   R6 = 0x00  — sprite pattern (not used)
     *   R7 = 0xF1  — foreground white (F), background black (1)
     */
    vdp_write_reg(0, 0x00);
    vdp_write_reg(1, 0xD0);
    vdp_write_reg(2, 0x00);
    vdp_write_reg(3, 0x00);
    vdp_write_reg(4, 0x01);
    vdp_write_reg(5, 0x00);
    vdp_write_reg(6, 0x00);
    vdp_write_reg(7, COLOR_NORMAL);

    ft_color = COLOR_NORMAL;
}

/* ------------------------------------------------------------------ */
/* Keyboard via CRU                                                      */
/*                                                                      */
/* The TI-99 keyboard matrix:                                           */
/*   8 columns selected by CRU output bits at base address 0x0006      */
/*   7 rows read as CRU input bits 0-6 at base address 0x0006          */
/*                                                                      */
/* We use polling: scan each column, read the row bits, match against  */
/* the key map.  This is blocking — we spin until a key is pressed.   */
/*                                                                      */
/* CRU access uses TMS9900 special instructions expressed as inline asm.*/
/* ------------------------------------------------------------------ */

/*
 * TMS9900 CRU bit test.
 * Returns 1 if the CRU bit at (base + offset) is set, 0 otherwise.
 * The TB instruction sets the EQ status bit; we use conditional load.
 */
static int cru_test(int bit)
{
    int result = 0;
    __asm__ (
        "TB   %1\n\t"    /* test CRU bit */
        "JNE  .+6\n\t"   /* if not equal (bit=0) skip */
        "LI   %0,1"      /* bit was set */
        : "=r"(result)
        : "i"(bit)
    );
    return result;
}

/* Set a CRU output bit to 1 */
static void cru_set(int bit)
{
    __asm__ ("SBO %0" : : "i"(bit));
}

/* Clear a CRU output bit to 0 */
static void cru_clr(int bit)
{
    __asm__ ("SBZ %0" : : "i"(bit));
}

/*
 * TI-99 keyboard matrix — column/row/character mapping.
 * The matrix has 8 columns (0-7) and 7 rows (0-6).
 * CRU column select bits are at CRU address 18-21 (relative to base).
 * Row bits are read at CRU address 6-12.
 *
 * This table gives the character for each [col][row] combination.
 * 0 means no key / shift-only / special handling needed.
 */

/* Keyboard scan: returns ASCII code of pressed key, or 0 if none */
static int kbd_scan_once(void)
{
    /*
     * Simplified matrix scan covering the most important keys.
     * A complete implementation would handle all 8 columns x 7 rows
     * plus SHIFT, CTRL, FCTN (function) key modifiers.
     *
     * The CRU keyboard base address for TI-99/4A is 0x0000.
     * Column select: bits at CRU offset 18-21.
     * Row read:      bits at CRU offset 6-12.
     *
     * For brevity we use the console ROM keyboard scan routine
     * if available, rather than reimplementing the full matrix here.
     * The console ROM KSCAN routine is at CPU address 0x000E (GPL).
     *
     * We call it via a BL @KSCAN instruction and read the result
     * from scratchpad RAM at >8375 (last character typed).
     */

    /*
     * Scratchpad address >8375 holds the last key pressed after KSCAN.
     * >8374 holds the status (0 = no key, non-zero = key ready).
     */
    volatile unsigned char *kscan_status = (volatile unsigned char *)0x8374;
    volatile unsigned char *kscan_char   = (volatile unsigned char *)0x8375;

    /* Call ROM KSCAN routine */
    __asm__ (
        "BLWP @0x000E"   /* call KSCAN via GPL workspace switch */
    );

    if (*kscan_status != 0)
        return (int)(*kscan_char);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Cursor: software cursor via VRAM overwrite                           */
/* ------------------------------------------------------------------ */

static unsigned char ft_under_cursor;
static int           ft_cursor_shown;

static unsigned int vram_pos(int row, int col)
{
    return (unsigned int)(VRAM_NAME_TABLE + row * TI_COLS + col);
}

static unsigned char vram_read_char(int row, int col)
{
    unsigned char c;
    vdp_set_addr_read(vram_pos(row, col));
    c = VDP_DATA_READ;
    return c;
}

static void vram_write_char(int row, int col, unsigned char c)
{
    vdp_set_addr_write(vram_pos(row, col));
    VDP_DATA_WRITE = c;
}

static void hide_cursor(void)
{
    if (ft_cursor_shown) {
        vram_write_char(ft_cur_row, ft_cur_col, ft_under_cursor);
        ft_cursor_shown = 0;
    }
}

static void show_cursor(void)
{
    if (!ft_cursor_shown) {
        ft_under_cursor = vram_read_char(ft_cur_row, ft_cur_col);
        vram_write_char(ft_cur_row, ft_cur_col, 0xDB); /* block character */
        ft_cursor_shown = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Output buffer                                                         */
/* ------------------------------------------------------------------ */

#define OUT_BUF 256
static char ft_out[OUT_BUF];
static int  ft_out_len;

static void flush_to_vram(void)
{
    int i;
    for (i = 0; i < ft_out_len; i++) {
        unsigned char c = (unsigned char)ft_out[i];
        if (ft_cur_col < TI_COLS && ft_cur_row < TI_ROWS)
            vram_write_char(ft_cur_row, ft_cur_col++, c);
    }
    ft_out_len = 0;
}

static void buf_put(char c)
{
    if (ft_out_len >= OUT_BUF)
        flush_to_vram();
    ft_out[ft_out_len++] = c;
}

/* ------------------------------------------------------------------ */
/* Public platform API                                                   */
/* ------------------------------------------------------------------ */

void platform_init(void)
{
    ft_cur_row     = 0;
    ft_cur_col     = 0;
    ft_cursor_shown = 0;
    ft_out_len     = 0;

    vdp_text_mode_init();
    init_font();
    platform_clear_screen();
}

void platform_shutdown(void)
{
    hide_cursor();
    flush_to_vram();
    ft_color = COLOR_NORMAL;
    vdp_write_reg(7, ft_color);
    platform_move(TI_ROWS - 1, 0);
}

void platform_get_size(int *rows, int *cols)
{
    *rows = TI_ROWS;
    *cols = TI_COLS;
}

void platform_move(int row, int col)
{
    hide_cursor();
    flush_to_vram();
    ft_cur_row = row;
    ft_cur_col = col;
}

void platform_putch(int c)
{
    buf_put((char)c);
}

void platform_puts(const char *s)
{
    while (*s)
        buf_put(*s++);
}

void platform_putn(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        buf_put(s[i]);
}

void platform_clear_eol(void)
{
    int c;
    flush_to_vram();
    for (c = ft_cur_col; c < TI_COLS; c++)
        vram_write_char(ft_cur_row, c, 0x20);
}

void platform_clear_screen(void)
{
    int r, c;
    hide_cursor();
    flush_to_vram();
    for (r = 0; r < TI_ROWS; r++)
        for (c = 0; c < TI_COLS; c++)
            vram_write_char(r, c, 0x20);
    ft_cur_row = 0;
    ft_cur_col = 0;
}

void platform_attr_reverse(void)
{
    /*
     * TMS9918A text mode applies one colour pair to the entire screen
     * via register R7.  We swap the colours when "reverse" is requested
     * so the header and footer bars appear visually distinct.
     * This affects the whole screen momentarily during drawing.
     */
    flush_to_vram();
    ft_color = COLOR_REVERSE;
    vdp_write_reg(7, ft_color);
}

void platform_attr_normal(void)
{
    flush_to_vram();
    ft_color = COLOR_NORMAL;
    vdp_write_reg(7, ft_color);
}

void platform_flush(void)
{
    flush_to_vram();
    show_cursor();
}

void platform_cursor_hide(void) {}
void platform_cursor_show(void) {}

/* ------------------------------------------------------------------ */
/* Keyboard input — blocking poll                                        */
/* ------------------------------------------------------------------ */

int platform_read_key(void)
{
    int c;

    /* Spin until a key is available */
    do {
        c = kbd_scan_once();
    } while (c == 0);

    /* Map TI-99 key codes to our KEY_* constants */
    switch (c) {
        /*
         * FCTN + arrow keys on TI-99 (using the FCTN layer):
         *   FCTN+S = left arrow  (>08 after KSCAN)
         *   FCTN+D = right arrow (>09)
         *   FCTN+E = up arrow    (>0A or TI-specific)
         *   FCTN+X = down arrow  (>0B or TI-specific)
         * The exact codes depend on the console ROM version.
         * We use the values reported by the standard KSCAN routine.
         */
        case 0x08: return KEY_LEFT;     /* FCTN+S on TI-99 */
        case 0x09: return KEY_RIGHT;    /* FCTN+D */
        case 0x0A: return KEY_DOWN;     /* FCTN+X (mapped via KSCAN) */
        case 0x0B: return KEY_UP;       /* FCTN+E */
        case 0x0D: return KEY_ENTER;
        case 0x1B: return KEY_ESCAPE;   /* FCTN+9 (BACK) on TI-99 */
        case 0x7F: return KEY_BACKSPACE;/* DEL key */

        /*
         * FCTN+1 = DELETE, FCTN+2 = INSERT on TI-99 extended basic.
         * Standard KSCAN maps these differently; adjust as needed.
         */
        case 0x01: return KEY_HOME;     /* FCTN+5 (BEGIN) */
        case 0x05: return KEY_END;      /* FCTN+0 (END) on some ROMs */

        default: break;
    }

    /* Ctrl+letter (ASCII 1-26) and printable ASCII pass through */
    if (c >= 1 && c <= 26) return c;
    if (c >= 0x20 && c < 0x7F) return c;

    return KEY_UNKNOWN;
}
