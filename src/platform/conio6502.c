/*
 * conio6502.c - cc65/6502 platform implementation for Forever Text
 *
 * Targets (selected at compile time via cc65's -t flag):
 *   Commodore 64 / 128   (cl65 -t c64 / -t c128)
 *   Atari 400/800/XL/XE  (cl65 -t atari)
 *   Apple II / IIe       (cl65 -t apple2)
 *
 * Uses cc65's <conio.h> for all terminal I/O.  The cc65 library
 * implements gotoxy(), cputc(), cgetc(), revers(), and screensize()
 * for each supported target so we get native character-mode I/O
 * without any OS terminal layer.
 *
 * Memory note:
 *   The 6502 machines typically have 38-64 KB of user RAM.  The
 *   editor will work but can only hold small files.  malloc() returns
 *   NULL when RAM is exhausted; the buffer layer handles this
 *   gracefully by refusing to grow further.
 *
 * Screen width note:
 *   Standard C64 and Atari screens are 40 columns wide.  The header
 *   ruler and title bar are designed to fit any width >= 40 columns.
 *   Apple II enhanced and 80-column card modes give 80 columns.
 */

#include <conio.h>
#include <string.h>
#include <stdlib.h>

/*
 * cc65's Atari conio.h (and possibly others) pre-defines KEY_UP, KEY_DOWN,
 * KEY_LEFT, KEY_RIGHT, KEY_DELETE, KEY_F1-F4, KEY_CTRL_L etc. with target-
 * specific raw key codes that differ from our platform-neutral values in
 * platform.h.  Undefine them all before including platform.h so our values
 * win, and the compiler does not report "Macro redefinition is not identical".
 */
#undef KEY_UP
#undef KEY_DOWN
#undef KEY_LEFT
#undef KEY_RIGHT
#undef KEY_HOME
#undef KEY_END
#undef KEY_DELETE
#undef KEY_INSERT
#undef KEY_TAB
#undef KEY_BACKSPACE
#undef KEY_CTRL_L
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10

#include "platform.h"

/* ------------------------------------------------------------------ */
/* Platform-specific key code mappings                                  */
/*                                                                      */
/* cc65's cgetc() returns raw character codes from the keyboard ROM.   */
/* These differ per machine family.  We map them to our KEY_* values.  */
/* ------------------------------------------------------------------ */

#if defined(__C64__) || defined(__C128__)

/* PETSCII codes returned by the C64/C128 KERNAL keyboard handler */
#define FT6502_UP       0x91  /* Cursor up   (PETSCII 145) */
#define FT6502_DOWN     0x11  /* Cursor down (PETSCII  17) */
#define FT6502_LEFT     0x9D  /* Cursor left (PETSCII 157) */
#define FT6502_RIGHT    0x1D  /* Cursor right(PETSCII  29) */
#define FT6502_HOME     0x13  /* HOME key    (PETSCII  19) */
#define FT6502_DEL      0x14  /* DEL key — acts as backspace on C64 */
#define FT6502_INST     0x94  /* INST key — forward delete */
#define FT6502_F1       0x85  /* F1 */
#define FT6502_F3       0x86  /* F3 */
#define FT6502_F5       0x87  /* F5 */
#define FT6502_F7       0x88  /* F7 */

#elif defined(__ATARI__)

/*
 * Atari OS returns "ATASCII" codes.  The cursor keys map to control
 * characters that the OS translates from the hardware key matrix.
 * Ctrl+letter combinations arrive as ASCII 1-26 as expected.
 */
#define FT6502_UP       0x1C  /* Ctrl+'-' on Atari keyboard matrix */
#define FT6502_DOWN     0x1D  /* Ctrl+'=' */
#define FT6502_LEFT     0x1E  /* Ctrl+'+' */
#define FT6502_RIGHT    0x1F  /* Ctrl+'*' */
#define FT6502_HOME     0x7C  /* Ctrl+Shift+< (clear screen / home) */
#define FT6502_DEL      0x7E  /* Backspace (Delete char left) */
#define FT6502_INST     0xFF  /* No true INSERT key — unused */
#define FT6502_F1       0xFF  /* Atari has HELP, not F-keys in BASIC mode */
#define FT6502_F3       0xFF
#define FT6502_F5       0xFF
#define FT6502_F7       0xFF

#elif defined(__APPLE2__) || defined(__APPLE2ENH__)

/*
 * Apple II open-apple/close-apple + arrow keys.
 * The enhanced Apple IIe returns ANSI-style codes via ProDOS.
 * Plain Apple II uses Ctrl+key for directional movement.
 */
#define FT6502_UP       0x0B  /* Ctrl+K */
#define FT6502_DOWN     0x0A  /* Ctrl+J */
#define FT6502_LEFT     0x08  /* Ctrl+H (also backspace) */
#define FT6502_RIGHT    0x15  /* Ctrl+U */
#define FT6502_HOME     0x01  /* Ctrl+A */
#define FT6502_DEL      0x7F  /* DEL character */
#define FT6502_INST     0xFF
#define FT6502_F1       0xFF
#define FT6502_F3       0xFF
#define FT6502_F5       0xFF
#define FT6502_F7       0xFF

#else

/* Generic fallback — may need tuning for other cc65 targets */
#define FT6502_UP       0x0B
#define FT6502_DOWN     0x0A
#define FT6502_LEFT     0x08
#define FT6502_RIGHT    0x0C
#define FT6502_HOME     0x01
#define FT6502_DEL      0x7F
#define FT6502_INST     0xFF
#define FT6502_F1       0xFF
#define FT6502_F3       0xFF
#define FT6502_F5       0xFF
#define FT6502_F7       0xFF

#endif

/* ------------------------------------------------------------------ */
/* State                                                                */
/* ------------------------------------------------------------------ */

static unsigned char ft_cols;
static unsigned char ft_rows;

/* ------------------------------------------------------------------ */
/* Output buffer (batch writes to reduce flicker)                       */
/* ------------------------------------------------------------------ */

#define OUT_BUF 512  /* modest size for 6502 RAM constraints */
static char ft_out[OUT_BUF];
static int  ft_out_len;

static void buf_flush_6502(void)
{
    int i;
    for (i = 0; i < ft_out_len; i++)
        cputc(ft_out[i]);
    ft_out_len = 0;
}

static void buf_putch_6502(char c)
{
    if (ft_out_len >= OUT_BUF)
        buf_flush_6502();
    ft_out[ft_out_len++] = c;
}

/* ------------------------------------------------------------------ */
/* Public platform API                                                   */
/* ------------------------------------------------------------------ */

void platform_init(void)
{
    screensize(&ft_cols, &ft_rows);
    clrscr();
}

void platform_shutdown(void)
{
    buf_flush_6502();
    revers(0);
    clrscr();
    gotoxy(0, 0);
}

void platform_get_size(int *rows, int *cols)
{
    *rows = (int)ft_rows;
    *cols = (int)ft_cols;
}

void platform_move(int row, int col)
{
    /* Flush before repositioning — conio gotoxy() moves the internal cursor */
    buf_flush_6502();
    /* cc65 gotoxy(x, y): x = column, y = row */
    gotoxy((unsigned char)col, (unsigned char)row);
}

void platform_putch(int c)
{
    buf_putch_6502((char)c);
}

void platform_puts(const char *s)
{
    while (*s)
        buf_putch_6502(*s++);
}

void platform_putn(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        buf_putch_6502(s[i]);
}

void platform_clear_eol(void)
{
    unsigned char x;
    buf_flush_6502();
    /*
     * cclreol() is not available in all cc65 target libraries.  Use wherex()
     * and write spaces to the end of the line instead — portable across all
     * cc65 targets.
     */
    x = wherex();
    while (x < ft_cols) {
        cputc(' ');
        x++;
    }
}

void platform_clear_screen(void)
{
    buf_flush_6502();
    clrscr();
}

void platform_attr_reverse(void)
{
    buf_flush_6502();
    revers(1);
}

void platform_attr_normal(void)
{
    buf_flush_6502();
    revers(0);
}

void platform_flush(void)
{
    buf_flush_6502();
}

void platform_cursor_hide(void) {}
void platform_cursor_show(void) {}

/* ------------------------------------------------------------------ */
/* Keyboard input                                                        */
/* ------------------------------------------------------------------ */

int platform_read_key(void)
{
    unsigned char c = (unsigned char)cgetc();

    /* Cursor and navigation keys */
    if (c == FT6502_UP)    return KEY_UP;
    if (c == FT6502_DOWN)  return KEY_DOWN;
    if (c == FT6502_LEFT)  return KEY_LEFT;
    if (c == FT6502_RIGHT) return KEY_RIGHT;
    if (c == FT6502_HOME)  return KEY_HOME;
    if (c == FT6502_DEL)   return KEY_BACKSPACE;
    if (c == FT6502_INST)  return KEY_DELETE;

    /* Function keys (C64 only — mapped to common commands) */
    if (c == FT6502_F1)    return KEY_F1;
    if (c == FT6502_F3)    return KEY_F3;
    if (c == FT6502_F5)    return KEY_F5;
    if (c == FT6502_F7)    return KEY_F7;

    /* Enter / Return */
    if (c == 0x0D)         return KEY_ENTER;

    /* Escape */
    if (c == 0x1B)         return KEY_ESCAPE;

    /* Ctrl+letter combinations arrive as ASCII 1-26 */
    if (c >= 1 && c <= 26) return (int)c;

    /* Printable ASCII */
    if (c >= 0x20 && c < 0x7F) return (int)c;

    return KEY_UNKNOWN;
}
