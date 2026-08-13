/*
 * win32.c - Windows Console API platform implementation for Forever Text
 *
 * Uses WriteConsoleOutput to render each frame atomically:
 *   - All draw calls (platform_move, platform_putch, platform_attr_*, etc.)
 *     write into a CHAR_INFO back-buffer in memory — zero Win32 calls.
 *   - platform_flush() sends the entire buffer to the console in one
 *     WriteConsoleOutput call, so no row-by-row scan is visible.
 *   - SetConsoleCursorPosition positions the hardware cursor afterwards.
 *   - platform_cursor_hide/show bracket each frame via SetConsoleCursorInfo.
 *
 * Works on Windows XP and later without ANSI/VT sequences.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#include "platform.h"

/* ------------------------------------------------------------------ */
/* Console handles and saved state                                       */
/* ------------------------------------------------------------------ */

static HANDLE ft_hout = INVALID_HANDLE_VALUE;
static HANDLE ft_hin  = INVALID_HANDLE_VALUE;
static DWORD  ft_orig_out_mode;
static DWORD  ft_orig_in_mode;
static WORD   ft_orig_attrs;
static DWORD  ft_orig_cursor_size;

/* ------------------------------------------------------------------ */
/* Attribute words                                                       */
/* ------------------------------------------------------------------ */

static WORD ft_normal_attr;   /* white on black */
static WORD ft_reverse_attr;  /* black on white */
static WORD ft_cur_attr;      /* attribute applied to the next write */

/* ------------------------------------------------------------------ */
/* CHAR_INFO back-buffer                                                 */
/*                                                                      */
/* All draw operations write here.  platform_flush() sends the whole   */
/* buffer to the console in a single WriteConsoleOutput call.           */
/* MAX_SCREEN_COLS x MAX_SCREEN_ROWS covers any practical console size. */
/* ------------------------------------------------------------------ */

#define MAX_SCREEN_COLS 220
#define MAX_SCREEN_ROWS 70

static CHAR_INFO ft_screen_buf[MAX_SCREEN_ROWS * MAX_SCREEN_COLS]; /* staging */
static CHAR_INFO ft_prev_buf[MAX_SCREEN_ROWS * MAX_SCREEN_COLS];   /* last flushed */

static int ft_cur_row    = 0;
static int ft_cur_col    = 0;
static int ft_screen_rows = 25;  /* updated by platform_get_size */
static int ft_screen_cols = 80;

/* ------------------------------------------------------------------ */
/* Public platform API                                                   */
/* ------------------------------------------------------------------ */

void platform_init(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    CONSOLE_CURSOR_INFO cci;

    ft_hout = GetStdHandle(STD_OUTPUT_HANDLE);
    ft_hin  = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(ft_hout, &ft_orig_out_mode);
    GetConsoleMode(ft_hin,  &ft_orig_in_mode);

    /* Disable processed output so we control all rendering */
    SetConsoleMode(ft_hout, 0);

    /* Raw input: no line buffering, no echo; catch resize events */
    SetConsoleMode(ft_hin, ENABLE_WINDOW_INPUT);

    GetConsoleScreenBufferInfo(ft_hout, &csbi);
    ft_orig_attrs = csbi.wAttributes;

    /* Normal = white on black, reverse = black on white */
    ft_normal_attr  = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    ft_reverse_attr = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
    ft_cur_attr     = ft_normal_attr;

    /* Save original cursor size so platform_cursor_show can restore it */
    if (GetConsoleCursorInfo(ft_hout, &cci))
        ft_orig_cursor_size = cci.dwSize;
    else
        ft_orig_cursor_size = 25;

    ft_cur_row     = 0;
    ft_cur_col     = 0;
    ft_screen_rows = 25;
    ft_screen_cols = 80;

    platform_clear_screen();
    platform_flush();
}

void platform_shutdown(void)
{
    platform_cursor_show();
    SetConsoleTextAttribute(ft_hout, ft_orig_attrs);
    SetConsoleMode(ft_hout, ft_orig_out_mode);
    SetConsoleMode(ft_hin,  ft_orig_in_mode);
}

void platform_get_size(int *rows, int *cols)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(ft_hout, &csbi)) {
        ft_screen_cols = (int)(csbi.srWindow.Right  - csbi.srWindow.Left + 1);
        ft_screen_rows = (int)(csbi.srWindow.Bottom - csbi.srWindow.Top  + 1);
        if (ft_screen_cols > MAX_SCREEN_COLS) ft_screen_cols = MAX_SCREEN_COLS;
        if (ft_screen_rows > MAX_SCREEN_ROWS) ft_screen_rows = MAX_SCREEN_ROWS;
        *cols = ft_screen_cols;
        *rows = ft_screen_rows;
    } else {
        ft_screen_rows = 25;
        ft_screen_cols = 80;
        *rows = 25;
        *cols = 80;
    }
}

/*
 * platform_move: record the logical cursor position for subsequent putch
 * calls.  No Win32 call is made here; SetConsoleCursorPosition happens
 * once in platform_flush after the full buffer is written.
 */
void platform_move(int row, int col)
{
    ft_cur_row = row;
    ft_cur_col = col;
}

void platform_putch(int c)
{
    int idx;
    if (ft_cur_row < 0 || ft_cur_row >= MAX_SCREEN_ROWS) return;
    if (ft_cur_col < 0 || ft_cur_col >= MAX_SCREEN_COLS) return;
    idx = ft_cur_row * MAX_SCREEN_COLS + ft_cur_col;
    ft_screen_buf[idx].Char.AsciiChar = (char)c;
    ft_screen_buf[idx].Attributes     = ft_cur_attr;
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

/*
 * platform_clear_eol: fill the rest of the current row with spaces up to
 * the actual screen width (ft_screen_cols, updated by platform_get_size).
 */
void platform_clear_eol(void)
{
    int col;
    int idx;
    if (ft_cur_row < 0 || ft_cur_row >= MAX_SCREEN_ROWS) return;
    for (col = ft_cur_col; col < ft_screen_cols; col++) {
        idx = ft_cur_row * MAX_SCREEN_COLS + col;
        ft_screen_buf[idx].Char.AsciiChar = ' ';
        ft_screen_buf[idx].Attributes     = ft_cur_attr;
    }
}

void platform_clear_screen(void)
{
    int r;
    int c;
    int idx;
    for (r = 0; r < ft_screen_rows; r++) {
        for (c = 0; c < ft_screen_cols; c++) {
            idx = r * MAX_SCREEN_COLS + c;
            ft_screen_buf[idx].Char.AsciiChar = ' ';
            ft_screen_buf[idx].Attributes     = ft_normal_attr;
        }
    }
    ft_cur_row = 0;
    ft_cur_col = 0;
}

/* Attribute changes are instant buffer state — no Win32 call needed */
void platform_attr_reverse(void) { ft_cur_attr = ft_reverse_attr; }
void platform_attr_normal(void)  { ft_cur_attr = ft_normal_attr;  }

/*
 * platform_flush: send the entire CHAR_INFO buffer to the console in one
 * atomic WriteConsoleOutput call, then position the hardware cursor.
 * Called once per frame after all draw functions have run.
 */
void platform_flush(void)
{
    COORD buf_size;
    COORD buf_origin;
    SMALL_RECT write_rect;
    COORD cursor_pos;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int top;

    if (ft_hout == INVALID_HANDLE_VALUE) return;

    GetConsoleScreenBufferInfo(ft_hout, &csbi);
    top = (int)csbi.srWindow.Top;

    /* Buffer layout: MAX_SCREEN_COLS wide, ft_screen_rows tall */
    buf_size.X   = (SHORT)MAX_SCREEN_COLS;
    buf_size.Y   = (SHORT)ft_screen_rows;
    buf_origin.X = 0;
    buf_origin.Y = 0;

    /* Destination rectangle: the visible console window */
    write_rect.Left   = csbi.srWindow.Left;
    write_rect.Top    = (SHORT)top;
    write_rect.Right  = (SHORT)(csbi.srWindow.Left + ft_screen_cols - 1);
    write_rect.Bottom = (SHORT)(top + ft_screen_rows - 1);

    /* Only call WriteConsoleOutput when content has changed */
    if (memcmp(ft_screen_buf, ft_prev_buf, sizeof(ft_screen_buf)) != 0) {
        WriteConsoleOutput(ft_hout, ft_screen_buf, buf_size, buf_origin, &write_rect);
        memcpy(ft_prev_buf, ft_screen_buf, sizeof(ft_prev_buf));
    }

    /* Always update hardware cursor position */
    cursor_pos.X = (SHORT)ft_cur_col;
    cursor_pos.Y = (SHORT)(top + ft_cur_row);
    SetConsoleCursorPosition(ft_hout, cursor_pos);
}

void platform_cursor_hide(void)
{
    CONSOLE_CURSOR_INFO cci;
    cci.dwSize   = 1;
    cci.bVisible = FALSE;
    SetConsoleCursorInfo(ft_hout, &cci);
}

void platform_cursor_show(void)
{
    CONSOLE_CURSOR_INFO cci;
    cci.dwSize   = (DWORD)ft_orig_cursor_size;
    cci.bVisible = TRUE;
    SetConsoleCursorInfo(ft_hout, &cci);
}

/* ------------------------------------------------------------------ */
/* Keyboard input                                                        */
/* ------------------------------------------------------------------ */

int platform_read_key(void)
{
    INPUT_RECORD ir;
    DWORD        read;
    KEY_EVENT_RECORD *ke;
    WORD  vk;
    DWORD ctrl;
    char  ascii;

    for (;;) {
        ReadConsoleInputA(ft_hin, &ir, 1, &read);
        if (read == 0) continue;

        /* Ignore everything except key-down events */
        if (ir.EventType != KEY_EVENT) continue;
        ke = &ir.Event.KeyEvent;
        if (!ke->bKeyDown) continue;

        vk    = ke->wVirtualKeyCode;
        ctrl  = ke->dwControlKeyState;
        ascii = ke->uChar.AsciiChar;

        /* Arrow and navigation keys */
        switch (vk) {
            case VK_UP:     return KEY_UP;
            case VK_DOWN:   return KEY_DOWN;
            case VK_LEFT:   return KEY_LEFT;
            case VK_RIGHT:  return KEY_RIGHT;
            case VK_PRIOR:  return KEY_PAGE_UP;
            case VK_NEXT:   return KEY_PAGE_DOWN;
            case VK_HOME:   return KEY_HOME;
            case VK_END:    return KEY_END;
            case VK_DELETE: return KEY_DELETE;
            case VK_F1:     return KEY_F1;
            case VK_F2:     return KEY_F2;
            case VK_F3:     return KEY_F3;
            case VK_F4:     return KEY_F4;
            case VK_F5:     return KEY_F5;
            case VK_F6:     return KEY_F6;
            case VK_F7:     return KEY_F7;
            case VK_F8:     return KEY_F8;
            case VK_F9:     return KEY_F9;
            case VK_F10:    return KEY_F10;
            default: break;
        }

        /* Suppress modifier-only keys */
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
            vk == VK_CAPITAL || vk == VK_NUMLOCK)
            continue;

        if (ascii != 0) {
            /* Ctrl+letter combinations arrive as ASCII 1-26 */
            return (int)(unsigned char)ascii;
        }

        /* Ignore anything else (IME, OEM keys, etc.) */
        (void)ctrl;
    }
}
