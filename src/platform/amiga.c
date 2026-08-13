/*
 * amiga.c - AmigaOS platform implementation for Forever Text
 *
 * Target: Commodore Amiga running AmigaOS 2.0 or later in a CLI/Shell window.
 *
 * Compiler: amiga-gcc (bebbo's GCC cross-compiler for Amiga)
 *   https://github.com/bebbo/amiga-gcc
 *
 * The Amiga Shell's console handler has supported ANSI/VT100 escape
 * sequences since AmigaOS 2.0 (1990), so we use the same escape codes
 * as ansi.c for all screen output.  The difference from ansi.c is how
 * we handle raw character input:
 *
 *   - POSIX uses tcsetattr() / termios to put the tty in raw mode.
 *   - AmigaDOS uses SetMode(Input(), 1) to switch the console from
 *     "cooked" (line-buffered) to "raw" (character-by-character) mode.
 *
 * Key mapping uses the AmigaDOS console device escape sequences.
 * Arrow keys on the Amiga send CSI A/B/C/D (0x9B is the Amiga's
 * single-byte CSI character), which is equivalent to ESC [ A/B/C/D.
 * We read and translate both forms.
 *
 * Screen size:
 *   We query the terminal via the ANSI DSR sequence (ESC [ 6n) which
 *   returns the cursor position after moving to a known far corner.
 *   Falls back to 80x24 if the query does not complete.
 */

#include <dos/dos.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"

/* ------------------------------------------------------------------ */
/* Handles                                                               */
/* ------------------------------------------------------------------ */

static BPTR ft_in;
static BPTR ft_out;

/* ------------------------------------------------------------------ */
/* Output helpers                                                        */
/* ------------------------------------------------------------------ */

static void ft_write(const char *s, int len)
{
    if (len > 0)
        Write(ft_out, (CONST APTR)s, (LONG)len);
}

static void ft_puts(const char *s)
{
    ft_write(s, (int)strlen(s));
}

static void ft_putch(char c)
{
    Write(ft_out, &c, 1);
}

/* Emit ESC [ followed by the given sequence string */
static void esc(const char *seq)
{
    ft_puts("\033[");
    ft_puts(seq);
}

/* Write an integer without using sprintf */
static void write_int(int n)
{
    char  buf[12];
    int   i = 0;

    if (n == 0) { ft_putch('0'); return; }
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i--)  ft_putch(buf[i]);
}

/* ------------------------------------------------------------------ */
/* Public platform API                                                   */
/* ------------------------------------------------------------------ */

void platform_init(void)
{
    ft_in  = Input();
    ft_out = Output();

    /* Switch console to raw mode: no line buffering, no echo */
    SetMode(ft_in, 1);
}

void platform_shutdown(void)
{
    /* Show cursor, reset attributes, move to a clean position */
    ft_puts("\033[?25h");
    esc("0m");
    ft_putch('\n');
    Flush(ft_out);

    /* Restore cooked mode */
    SetMode(ft_in, 0);
}

void platform_get_size(int *rows, int *cols)
{
    /*
     * Move to a far corner then query cursor position via ANSI DSR.
     * Response arrives as ESC [ rows ; cols R on the input stream.
     */
    char  buf[32];
    int   i = 0;
    int   r = 24, c = 80;
    UBYTE ch;

    ft_puts("\033[999;999H\033[6n");
    Flush(ft_out);

    while (i < (int)sizeof(buf) - 1) {
        if (Read(ft_in, &ch, 1) != 1) break;
        buf[i++] = (char)ch;
        if (ch == 'R') break;
    }
    buf[i] = '\0';

    if (sscanf(buf, "\033[%d;%dR", &r, &c) == 2) {
        *rows = r;
        *cols = c;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

void platform_move(int row, int col)
{
    /* ANSI uses 1-based row;col */
    ft_puts("\033[");
    write_int(row + 1);
    ft_putch(';');
    write_int(col + 1);
    ft_putch('H');
}

void platform_putch(int c)
{
    char ch = (char)c;
    Write(ft_out, &ch, 1);
}

void platform_puts(const char *s)
{
    ft_puts(s);
}

void platform_putn(const char *s, int n)
{
    ft_write(s, n);
}

void platform_clear_eol(void)
{
    esc("K");
}

void platform_clear_screen(void)
{
    esc("2J");
    esc("H");
}

void platform_attr_reverse(void)
{
    esc("7m");
}

void platform_attr_normal(void)
{
    esc("0m");
}

void platform_flush(void)
{
    Flush(ft_out);
}

void platform_cursor_hide(void)
{
    ft_puts("\033[?25l");
    Flush(ft_out);
}

void platform_cursor_show(void)
{
    ft_puts("\033[?25h");
    Flush(ft_out);
}

/* ------------------------------------------------------------------ */
/* Keyboard input                                                        */
/* ------------------------------------------------------------------ */

static int read_byte(void)
{
    UBYTE c;
    if (Read(ft_in, &c, 1) == 1)
        return (int)c;
    return -1;
}

int platform_read_key(void)
{
    int c = read_byte();
    if (c < 0) return KEY_UNKNOWN;

    /*
     * The Amiga uses 0x9B as a single-byte CSI (equivalent to ESC [).
     * Arrow keys and function keys arrive as 0x9B followed by a letter.
     * We also handle the two-byte ESC [ form for compatibility.
     */
    if (c == 0x9B || c == 0x1B) {
        int c2;

        if (c == 0x1B) {
            /* Two-byte ESC: must be followed by '[' */
            c2 = read_byte();
            if (c2 < 0) return KEY_ESCAPE;
            if (c2 != '[' && c2 != 'O') return KEY_ESCAPE;
        }

        c2 = read_byte();
        switch (c2) {
            case 'A': return KEY_UP;
            case 'B': return KEY_DOWN;
            case 'C': return KEY_RIGHT;
            case 'D': return KEY_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
            default:  break;
        }

        /* Numeric parameter sequences: CSI N ~ */
        if (c2 >= '0' && c2 <= '9') {
            int num = c2 - '0';
            int c3  = read_byte();

            if (c3 >= '0' && c3 <= '9') {
                num = num * 10 + (c3 - '0');
                c3  = read_byte();
            }

            if (c3 == '~') {
                switch (num) {
                    case 1: return KEY_HOME;
                    case 3: return KEY_DELETE;
                    case 4: return KEY_END;
                    case 5: return KEY_PAGE_UP;
                    case 6: return KEY_PAGE_DOWN;
                    default: return KEY_UNKNOWN;
                }
            }
        }

        return KEY_UNKNOWN;
    }

    if (c == 13)  return KEY_ENTER;
    if (c == 8 || c == 127) return KEY_BACKSPACE;

    /* Ctrl+letter and printable ASCII pass through directly */
    return c;
}
