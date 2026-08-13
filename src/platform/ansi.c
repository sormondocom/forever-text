/*
 * ansi.c - ANSI/VT100 platform implementation for Forever Text
 *
 * Works on:
 *   - Linux, macOS, BSDs (via POSIX termios + TIOCGWINSZ)
 *   - VAX/VMS with a VT100-compatible terminal
 *   - DOS with ANSI.SYS loaded (uses conio.h for raw input)
 *   - Any system presenting a VT100-compatible terminal
 *
 * Rendering uses a staging + display cell buffer (Option B):
 *   - Draw calls (platform_move, platform_putch, etc.) write to ft_staging.
 *   - platform_flush() diffs ft_staging against ft_display and emits only
 *     changed cells as minimal escape sequences, then copies staging->display.
 *   - This minimises bytes sent to the terminal, which matters most on slow
 *     serial links (e.g. VT100 at 9600 baud).
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

/* Integer-to-terminal helper — avoids sprintf for C89 portability */
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
/* Cell staging and display buffers                                      */
/*                                                                      */
/* ft_staging: the frame we are currently building (draw calls land here)
 * ft_display: what is currently rendered on the physical terminal      */
/* ------------------------------------------------------------------ */

typedef struct {
    char          c;    /* character to display                         */
    unsigned char attr; /* 0 = normal, 1 = reverse video               */
} FtCell;

#define FT_ANSI_MAX_ROWS 70
#define FT_ANSI_MAX_COLS 220

static FtCell        ft_staging[FT_ANSI_MAX_ROWS * FT_ANSI_MAX_COLS];
static FtCell        ft_display[FT_ANSI_MAX_ROWS * FT_ANSI_MAX_COLS];
static int           ft_scr_rows  = 24;
static int           ft_scr_cols  = 80;
static int           ft_cur_row   = 0;   /* logical cursor (set by platform_move) */
static int           ft_cur_col   = 0;
static unsigned char ft_cur_attr  = 0;   /* 0=normal, 1=reverse */
static int           ft_dirty_all = 0;   /* 1 = skip diff, redraw everything */

/* ------------------------------------------------------------------ */
/* Public platform API                                                   */
/* ------------------------------------------------------------------ */

void platform_init(void)
{
#ifdef FT_POSIX
    posix_enable_raw();
#endif
    fputs("\033[0m", stdout); /* reset any lingering attributes */
    fflush(stdout);
    platform_clear_screen();
    platform_flush();
}

void platform_shutdown(void)
{
    /* Reset attrs and ensure cursor is visible before returning to shell */
    fputs("\033[0m\033[?25h", stdout);
    fputc('\n', stdout);
    fflush(stdout);
#ifdef FT_POSIX
    posix_disable_raw();
#endif
}

void platform_get_size(int *rows, int *cols)
{
    int new_rows;
    int new_cols;

#ifdef FT_POSIX
    {
        struct winsize ws;
        new_rows = 24;
        new_cols = 80;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
            ws.ws_row > 0 && ws.ws_col > 0) {
            new_rows = (int)ws.ws_row;
            new_cols = (int)ws.ws_col;
        } else {
            /*
             * Fallback: move cursor to far corner, query position via DSR.
             * These writes go directly to stdout outside the staging model;
             * the next platform_flush restores the correct screen state.
             */
            char buf[32];
            int  i = 0;
            int  ch;
            fputs("\033[999;999H\033[6n", stdout);
            fflush(stdout);
            while (i < (int)(sizeof(buf) - 1)) {
                ch = posix_read_byte();
                if (ch < 0) break;
                buf[i++] = (char)ch;
                if (ch == 'R') break;
            }
            buf[i] = '\0';
            if (sscanf(buf, "\033[%d;%dR", &new_rows, &new_cols) != 2) {
                new_rows = 24;
                new_cols = 80;
            }
        }
    }
#elif defined(FT_DOS)
    new_rows = 25;
    new_cols = 80;
#else
    new_rows = 24;
    new_cols = 80;
#endif

    if (new_rows > FT_ANSI_MAX_ROWS) new_rows = FT_ANSI_MAX_ROWS;
    if (new_cols > FT_ANSI_MAX_COLS) new_cols = FT_ANSI_MAX_COLS;
    if (new_rows < 1) new_rows = 1;
    if (new_cols < 1) new_cols = 1;

    /* Force a full redraw if the terminal was resized */
    if (new_rows != ft_scr_rows || new_cols != ft_scr_cols)
        ft_dirty_all = 1;

    ft_scr_rows = new_rows;
    ft_scr_cols = new_cols;
    *rows = ft_scr_rows;
    *cols = ft_scr_cols;
}

/* Record logical cursor position; no output until platform_flush */
void platform_move(int row, int col)
{
    ft_cur_row = row;
    ft_cur_col = col;
}

void platform_putch(int c)
{
    int idx;
    if (ft_cur_row < 0 || ft_cur_row >= FT_ANSI_MAX_ROWS) return;
    if (ft_cur_col < 0 || ft_cur_col >= FT_ANSI_MAX_COLS) return;
    idx = ft_cur_row * FT_ANSI_MAX_COLS + ft_cur_col;
    ft_staging[idx].c    = (char)c;
    ft_staging[idx].attr = ft_cur_attr;
    ft_cur_col++;
}

void platform_puts(const char *s)
{
    while (*s)
        platform_putch((unsigned char)*s++);
}

void platform_putn(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        platform_putch((unsigned char)s[i]);
}

/* Fill the remainder of the current row in staging with spaces */
void platform_clear_eol(void)
{
    int col;
    int idx;
    if (ft_cur_row < 0 || ft_cur_row >= FT_ANSI_MAX_ROWS) return;
    for (col = ft_cur_col; col < ft_scr_cols; col++) {
        if (col >= FT_ANSI_MAX_COLS) break;
        idx = ft_cur_row * FT_ANSI_MAX_COLS + col;
        ft_staging[idx].c    = ' ';
        ft_staging[idx].attr = ft_cur_attr;
    }
}

/* Fill entire staging buffer with spaces and mark for full redraw */
void platform_clear_screen(void)
{
    int r;
    int c;
    int idx;
    for (r = 0; r < FT_ANSI_MAX_ROWS; r++) {
        for (c = 0; c < FT_ANSI_MAX_COLS; c++) {
            idx = r * FT_ANSI_MAX_COLS + c;
            ft_staging[idx].c    = ' ';
            ft_staging[idx].attr = 0;
        }
    }
    ft_cur_row   = 0;
    ft_cur_col   = 0;
    ft_dirty_all = 1;
}

void platform_attr_reverse(void) { ft_cur_attr = 1; }
void platform_attr_normal(void)  { ft_cur_attr = 0; }

/*
 * platform_flush: diff ft_staging against ft_display; emit only changed
 * cells as minimal ANSI escape sequences; copy staging->display; then
 * position the hardware cursor at the location set by platform_move.
 *
 * Consecutive changed cells within the same row are emitted as a run —
 * one move sequence at the start, then characters without re-positioning
 * as the terminal cursor advances automatically after each character.
 */
void platform_flush(void)
{
    int     row;
    int     col;
    int     idx;
    int     dirty_all;
    int     hw_row;     /* terminal cursor's current row  */
    int     hw_col;     /* terminal cursor's current col  */
    int     hw_attr;    /* attribute active in terminal (-1 = unknown) */
    FtCell *s;
    FtCell *d;

    dirty_all = ft_dirty_all;
    ft_dirty_all = 0;

    hw_row  = -1;
    hw_col  = -1;
    hw_attr = -1;

    for (row = 0; row < ft_scr_rows; row++) {
        for (col = 0; col < ft_scr_cols; col++) {
            idx = row * FT_ANSI_MAX_COLS + col;
            s   = &ft_staging[idx];
            d   = &ft_display[idx];

            if (!dirty_all && s->c == d->c && s->attr == d->attr)
                continue;

            /* Move cursor only when it is not already at this cell */
            if (row != hw_row || col != hw_col) {
                fputc('\033', stdout);
                fputc('[',    stdout);
                write_int(row + 1);
                fputc(';',    stdout);
                write_int(col + 1);
                fputc('H',    stdout);
                hw_row = row;
                hw_col = col;
            }

            /* Change attribute only when it differs from current */
            if ((int)s->attr != hw_attr) {
                if (s->attr)
                    esc("7m");
                else
                    esc("0m");
                hw_attr = (int)s->attr;
            }

            fputc(s->c, stdout);
            hw_col++;  /* terminal cursor advances one column after each char */
            *d = *s;
        }
    }

    /* If we left the terminal in reverse video, reset to normal */
    if (hw_attr == 1)
        esc("0m");

    /* Position hardware cursor at the logical cursor location */
    fputc('\033', stdout);
    fputc('[',    stdout);
    write_int(ft_cur_row + 1);
    fputc(';',    stdout);
    write_int(ft_cur_col + 1);
    fputc('H',    stdout);

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
