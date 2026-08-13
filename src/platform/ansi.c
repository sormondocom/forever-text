/*
 * ansi.c - ANSI/VT100 platform implementation for Forever Text
 *
 * Works on:
 *   - Linux, macOS, BSDs (via POSIX termios + TIOCGWINSZ)
 *   - VAX/VMS with a VT100-compatible terminal
 *   - DOS with ANSI.SYS loaded (uses conio.h for raw input)
 *   - Any system presenting a VT100-compatible terminal
 *
 * Compile-time detection:
 *   MSDOS / __MSDOS__ / _MSDOS   -> DOS path (conio.h, no termios)
 *   Otherwise                    -> POSIX path (termios, sys/ioctl)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

/* ------------------------------------------------------------------ */
/* Platform-specific includes and raw-mode state                        */
/* ------------------------------------------------------------------ */

#if defined(MSDOS) || defined(__MSDOS__) || defined(_MSDOS) || defined(__DOS__)
#define FT_DOS 1
#include <conio.h>

#else
#define FT_POSIX 1
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

static struct termios ft_orig_termios;

static void posix_enable_raw(void)
{
    struct termios raw;
    tcgetattr(STDIN_FILENO, &ft_orig_termios);
    raw = ft_orig_termios;
    /* Input: no Ctrl+C signal, no CR/NL translation, no flow control */
    raw.c_iflag &= ~(unsigned long)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* Output: write bytes as-is (we send our own escape sequences) */
    raw.c_oflag &= ~(unsigned long)(OPOST);
    /* Character size 8 bits */
    raw.c_cflag |= (unsigned long)(CS8);
    /* Local: no canonical mode, no echo, no signals, no extended processing */
    raw.c_lflag &= ~(unsigned long)(ECHO | ICANON | IEXTEN | ISIG);
    /* read() returns after at least 1 byte, no timeout */
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void posix_disable_raw(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &ft_orig_termios);
}

static int posix_read_byte(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) == 1)
        return (int)c;
    return -1;
}

/* Non-blocking read: returns -1 immediately if no byte available */
static int posix_read_byte_nb(void)
{
    struct termios nb;
    int c;
    tcgetattr(STDIN_FILENO, &nb);
    nb.c_cc[VMIN]  = 0;
    nb.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &nb);
    c = posix_read_byte();
    nb.c_cc[VMIN]  = 1;
    nb.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &nb);
    return c;
}

#endif /* FT_POSIX */

/* ------------------------------------------------------------------ */
/* ANSI escape sequence helpers                                          */
/* ------------------------------------------------------------------ */

static void esc(const char *seq)
{
    fputs("\033[", stdout);
    fputs(seq, stdout);
}

/* Simple integer-to-string for building escape sequences without sprintf */
static void write_int(int n)
{
    char buf[16];
    int  i = 0;
    int  neg = 0;

    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) { fputc('0', stdout); return; }

    while (n > 0) {
        buf[i++] = (char)('0' + n % 10);
        n /= 10;
    }
    if (neg) buf[i++] = '-';

    while (i--) fputc(buf[i], stdout);
}

/* ------------------------------------------------------------------ */
/* Public platform API                                                   */
/* ------------------------------------------------------------------ */

void platform_init(void)
{
#ifdef FT_POSIX
    posix_enable_raw();
#endif
    platform_clear_screen();
    platform_flush();
}

void platform_shutdown(void)
{
    /* Show cursor, move to a clean bottom position */
    fputs("\033[?25h", stdout);
    esc("0m");           /* reset all attributes */
    fputc('\n', stdout);
    platform_flush();
#ifdef FT_POSIX
    posix_disable_raw();
#endif
}

void platform_get_size(int *rows, int *cols)
{
#ifdef FT_POSIX
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = (int)ws.ws_row;
        *cols = (int)ws.ws_col;
        return;
    }
    /* Fallback: move to far corner and query cursor position */
    {
        int r = 24, c = 80;
        char buf[32];
        int  i = 0;
        int  ch;

        fputs("\033[999;999H\033[6n", stdout);
        fflush(stdout);

        /* Read ESC [ rows ; cols R */
        while (i < (int)(sizeof(buf) - 1)) {
            ch = posix_read_byte();
            if (ch < 0) break;
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
#elif defined(FT_DOS)
    /* Standard DOS text mode is 80x25; query BIOS for actual mode */
    *rows = 25;
    *cols = 80;
#else
    *rows = 24;
    *cols = 80;
#endif
}

void platform_move(int row, int col)
{
    /* ANSI uses 1-based coordinates */
    fputc('\033', stdout);
    fputc('[',   stdout);
    write_int(row + 1);
    fputc(';',   stdout);
    write_int(col + 1);
    fputc('H',   stdout);
}

void platform_putch(int c)
{
    fputc(c, stdout);
}

void platform_puts(const char *s)
{
    fputs(s, stdout);
}

void platform_putn(const char *s, int n)
{
    fwrite(s, 1, (size_t)n, stdout);
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
    fflush(stdout);
}

void platform_cursor_hide(void)
{
    fputs("\033[?25l", stdout);
    fflush(stdout);
}

void platform_cursor_show(void)
{
    fputs("\033[?25h", stdout);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Keyboard input                                                        */
/* ------------------------------------------------------------------ */

int platform_read_key(void)
{
    int c;

#ifdef FT_DOS
    c = getch();
    if (c == 0 || c == 0xE0) {
        /* Extended key: second byte is the scan code */
        int ext = getch();
        switch (ext) {
            case 72: return KEY_UP;
            case 80: return KEY_DOWN;
            case 75: return KEY_LEFT;
            case 77: return KEY_RIGHT;
            case 73: return KEY_PAGE_UP;
            case 81: return KEY_PAGE_DOWN;
            case 71: return KEY_HOME;
            case 79: return KEY_END;
            case 83: return KEY_DELETE;
            case 59: return KEY_F1;
            case 60: return KEY_F2;
            case 61: return KEY_F3;
            case 62: return KEY_F4;
            case 63: return KEY_F5;
            case 64: return KEY_F6;
            case 65: return KEY_F7;
            case 66: return KEY_F8;
            case 67: return KEY_F9;
            case 68: return KEY_F10;
            default: return KEY_UNKNOWN;
        }
    }
    /* Map DOS's 13 (Enter) and 8 (Backspace) directly */
    return c;

#else  /* POSIX path */
    c = posix_read_byte();
    if (c < 0)
        return KEY_UNKNOWN;

    if (c != 27)   /* Not an escape — return directly */
        return c;

    /* Escape: peek at the next byte to distinguish ESC alone from sequences */
    {
        int c2 = posix_read_byte_nb();
        if (c2 < 0)
            return KEY_ESCAPE;   /* bare ESC key */

        if (c2 != '[' && c2 != 'O')
            return KEY_ESCAPE;   /* ESC followed by something else */

        /* CSI or SS3 sequence */
        {
            int c3 = posix_read_byte();
            switch (c3) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
                case 'P': return KEY_F1;
                case 'Q': return KEY_F2;
                case 'R': return KEY_F3;
                case 'S': return KEY_F4;
                default:  break;
            }

            /* Sequences with a numeric parameter: ESC [ N ~ */
            if (c3 >= '0' && c3 <= '9') {
                int num = c3 - '0';
                int c4  = posix_read_byte();

                /* Two-digit parameter */
                if (c4 >= '0' && c4 <= '9') {
                    num = num * 10 + (c4 - '0');
                    c4  = posix_read_byte();
                }

                if (c4 == '~') {
                    switch (num) {
                        case 1:  return KEY_HOME;
                        case 3:  return KEY_DELETE;
                        case 4:  return KEY_END;
                        case 5:  return KEY_PAGE_UP;
                        case 6:  return KEY_PAGE_DOWN;
                        case 11: return KEY_F1;
                        case 12: return KEY_F2;
                        case 13: return KEY_F3;
                        case 14: return KEY_F4;
                        case 15: return KEY_F5;
                        case 17: return KEY_F6;
                        case 18: return KEY_F7;
                        case 19: return KEY_F8;
                        case 20: return KEY_F9;
                        case 21: return KEY_F10;
                        default: return KEY_UNKNOWN;
                    }
                }
            }
        }
    }

    return KEY_UNKNOWN;
#endif
}
