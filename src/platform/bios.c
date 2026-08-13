/*
 * bios.c - DOS BIOS direct platform implementation for Forever Text
 *
 * Uses INT 10h (video) and INT 16h (keyboard) directly.
 * Does NOT require ANSI.SYS to be loaded.
 * Target: 16-bit real-mode DOS (ia16-elf-gcc, Open Watcom, Turbo C).
 *
 * This is the lowest-level target: it talks to the hardware through
 * the BIOS interrupt table and requires no operating system services
 * beyond what the BIOS ROM provides at boot.
 *
 * Tested/expected compilers:
 *   - ia16-elf-gcc (GCC cross-compiler for 8086 real mode)
 *   - Open Watcom C (watcom target: dos16)
 *   - Borland Turbo C 2.0
 */

#include <stdio.h>
#include "platform.h"

/* ------------------------------------------------------------------ */
/* BIOS call wrappers                                                    */
/*                                                                      */
/* Most C compilers targeting 16-bit DOS provide either __dpmi_int()   */
/* (DJGPP), int86() (Watcom / Turbo C / MSC), or inline __asm.         */
/* We use #ifdefs to select the right mechanism.                        */
/* ------------------------------------------------------------------ */

#if defined(__WATCOMC__) || defined(__TURBOC__) || defined(_MSC_VER)
/* These compilers provide <dos.h> with int86() or geninterrupt() */
#include <dos.h>

static unsigned char bios_attr = 0x07; /* light grey on black */
static int bios_rows = 25;
static int bios_cols = 80;

static void bios_write_char_at(int row, int col, char c, unsigned char attr)
{
    union REGS r;
    /* INT 10h AH=02h: Set cursor position */
    r.h.ah = 0x02;
    r.h.bh = 0x00;   /* page 0 */
    r.h.dh = (unsigned char)row;
    r.h.dl = (unsigned char)col;
    int86(0x10, &r, &r);
    /* INT 10h AH=09h: Write character and attribute at cursor */
    r.h.ah = 0x09;
    r.h.al = (unsigned char)c;
    r.h.bh = 0x00;
    r.h.bl = attr;
    r.x.cx = 1;
    int86(0x10, &r, &r);
}

static int bios_read_key_raw(void)
{
    union REGS r;
    /* INT 16h AH=00h: Read character from keyboard */
    r.h.ah = 0x00;
    int86(0x16, &r, &r);
    /* AL = ASCII, AH = scan code */
    return (int)r.h.ax; /* high byte = scan, low byte = ascii */
}

#elif defined(__ia16__) || defined(__IA16__)
/* ia16-elf-gcc: use inline assembly */
#include <stdint.h>

static unsigned char bios_attr = 0x07;
static int bios_rows = 25;
static int bios_cols = 80;

static void bios_write_char_at(int row, int col, char c, unsigned char attr)
{
    /* Set cursor */
    __asm__ __volatile__ (
        "int $0x10"
        :
        : "a"((uint16_t)0x0200),
          "b"((uint16_t)0x0000),
          "d"((uint16_t)((row << 8) | col))
        : "cc"
    );
    /* Write char + attr */
    __asm__ __volatile__ (
        "int $0x10"
        :
        : "a"((uint16_t)(0x0900 | (unsigned char)c)),
          "b"((uint16_t)attr),
          "c"((uint16_t)1)
        : "cc"
    );
}

static int bios_read_key_raw(void)
{
    uint16_t result;
    __asm__ __volatile__ (
        "int $0x16"
        : "=a"(result)
        : "a"((uint16_t)0x0000)
        : "cc"
    );
    return (int)result;
}

#else
/* Stub for compilation on non-DOS targets (for testing structure only) */
static unsigned char bios_attr = 0x07;
static int bios_rows = 25;
static int bios_cols = 80;

static void bios_write_char_at(int row, int col, char c, unsigned char attr)
{
    (void)row; (void)col; (void)c; (void)attr;
}

static int bios_read_key_raw(void)
{
    return 0;
}
#endif

/* ------------------------------------------------------------------ */
/* Cursor tracking (BIOS does not buffer position for us)               */
/* ------------------------------------------------------------------ */

static int bios_cur_row = 0;
static int bios_cur_col = 0;

/* ------------------------------------------------------------------ */
/* Output buffer (write to video memory via BIOS in one pass)           */
/* ------------------------------------------------------------------ */

static char  bios_out[4096];
static int   bios_out_len;

/* ------------------------------------------------------------------ */
/* Public platform API                                                   */
/* ------------------------------------------------------------------ */

void platform_init(void)
{
    bios_rows      = 25;
    bios_cols      = 80;
    bios_attr      = 0x07;
    bios_cur_row   = 0;
    bios_cur_col   = 0;
    bios_out_len   = 0;
    platform_clear_screen();
}

void platform_shutdown(void)
{
    platform_flush();
    bios_attr = 0x07;
    platform_move(bios_rows - 1, 0);
}

void platform_get_size(int *rows, int *cols)
{
    *rows = bios_rows;
    *cols = bios_cols;
}

void platform_move(int row, int col)
{
    platform_flush();
    bios_cur_row = row;
    bios_cur_col = col;
}

void platform_putch(int c)
{
    if (bios_out_len < (int)sizeof(bios_out) - 1)
        bios_out[bios_out_len++] = (char)c;
    else
        platform_flush();
}

void platform_puts(const char *s)
{
    while (*s)
        platform_putch(*s++);
}

void platform_putn(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        platform_putch(s[i]);
}

void platform_clear_eol(void)
{
    int c;
    platform_flush();
    for (c = bios_cur_col; c < bios_cols; c++)
        bios_write_char_at(bios_cur_row, c, ' ', bios_attr);
}

void platform_clear_screen(void)
{
    int r, c;
    platform_flush();
    for (r = 0; r < bios_rows; r++)
        for (c = 0; c < bios_cols; c++)
            bios_write_char_at(r, c, ' ', bios_attr);
    bios_cur_row = 0;
    bios_cur_col = 0;
}

void platform_attr_reverse(void)
{
    platform_flush();
    /* Swap foreground (bits 0-3) and background (bits 4-6) */
    bios_attr = 0x70; /* white background, black text */
}

void platform_attr_normal(void)
{
    platform_flush();
    bios_attr = 0x07; /* light grey on black */
}

void platform_cursor_hide(void)
{
#if defined(__WATCOMC__) || defined(__TURBOC__) || defined(_MSC_VER)
    union REGS r;
    r.h.ah = 0x01;
    r.h.ch = 0x20; /* bit 5 of CH set = cursor not visible */
    r.h.cl = 0x00;
    int86(0x10, &r, &r);
#elif defined(__ia16__) || defined(__IA16__)
    __asm__ __volatile__(
        "int $0x10"
        :
        : "a"((uint16_t)0x0100), "c"((uint16_t)0x2000)
        : "cc"
    );
#else
    (void)0;
#endif
}

void platform_cursor_show(void)
{
#if defined(__WATCOMC__) || defined(__TURBOC__) || defined(_MSC_VER)
    union REGS r;
    r.h.ah = 0x01;
    r.h.ch = 0x06; /* normal blinking cursor scan lines 6-7 */
    r.h.cl = 0x07;
    int86(0x10, &r, &r);
#elif defined(__ia16__) || defined(__IA16__)
    __asm__ __volatile__(
        "int $0x10"
        :
        : "a"((uint16_t)0x0100), "c"((uint16_t)0x0607)
        : "cc"
    );
#else
    (void)0;
#endif
}

void platform_flush(void)
{
    int i;
    for (i = 0; i < bios_out_len; i++) {
        char c = bios_out[i];
        if (c == '\n') {
            bios_cur_row++;
            bios_cur_col = 0;
        } else if (c == '\r') {
            bios_cur_col = 0;
        } else {
            bios_write_char_at(bios_cur_row, bios_cur_col, c, bios_attr);
            bios_cur_col++;
            if (bios_cur_col >= bios_cols) {
                bios_cur_col = 0;
                bios_cur_row++;
            }
        }
    }
    bios_out_len = 0;
}

int platform_read_key(void)
{
    int raw;
    int ascii;
    int scan;

    raw   = bios_read_key_raw();
    ascii = raw & 0xFF;
    scan  = (raw >> 8) & 0xFF;

    if (ascii != 0) {
        /* Normal key or Ctrl+key */
        if (ascii == 13) return KEY_ENTER;
        if (ascii == 8)  return KEY_BACKSPACE;
        if (ascii == 27) return KEY_ESCAPE;
        return ascii;
    }

    /* Extended key (ascii==0): use scan code */
    switch (scan) {
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x49: return KEY_PAGE_UP;
        case 0x51: return KEY_PAGE_DOWN;
        case 0x47: return KEY_HOME;
        case 0x4F: return KEY_END;
        case 0x53: return KEY_DELETE;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        default:   return KEY_UNKNOWN;
    }
}
