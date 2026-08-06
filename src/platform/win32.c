/*
 * win32.c - Windows Console API platform implementation for Forever Text
 *
 * Uses the Win32 Console API directly for:
 *   - Raw key input (ReadConsoleInput)
 *   - Cursor positioning (SetConsoleCursorPosition)
 *   - Reverse video (SetConsoleTextAttribute)
 *   - Screen size (GetConsoleScreenBufferInfo)
 *
 * This works on Windows XP and later without requiring ANSI/VT sequences.
 * For Windows 10 1511+ you could alternatively enable VT processing and
 * use ansi.c, but this implementation is the broadest Win32 target.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "platform.h"

/* ------------------------------------------------------------------ */
/* Console handles and saved state                                       */
/* ------------------------------------------------------------------ */

static HANDLE ft_hout = INVALID_HANDLE_VALUE;
static HANDLE ft_hin  = INVALID_HANDLE_VALUE;
static DWORD  ft_orig_out_mode;
static DWORD  ft_orig_in_mode;
static WORD   ft_orig_attrs;

/* Current attribute word (foreground + background) */
static WORD   ft_normal_attr;
static WORD   ft_reverse_attr;

/* ------------------------------------------------------------------ */
/* Output buffer                                                         */
/* ------------------------------------------------------------------ */

#define OUT_BUF_SIZE 65536
static char  ft_out_buf[OUT_BUF_SIZE];
static int   ft_out_pos;

static void buf_flush(void)
{
    DWORD written;
    if (ft_out_pos > 0 && ft_hout != INVALID_HANDLE_VALUE) {
        WriteConsoleA(ft_hout, ft_out_buf, (DWORD)ft_out_pos, &written, NULL);
        ft_out_pos = 0;
    }
}

static void buf_putch(char c)
{
    if (ft_out_pos >= OUT_BUF_SIZE)
        buf_flush();
    ft_out_buf[ft_out_pos++] = c;
}

static void buf_puts(const char *s)
{
    while (*s)
        buf_putch(*s++);
}

static void buf_putn(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        buf_putch(s[i]);
}

/* ------------------------------------------------------------------ */
/* Public platform API                                                   */
/* ------------------------------------------------------------------ */

void platform_init(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    ft_hout = GetStdHandle(STD_OUTPUT_HANDLE);
    ft_hin  = GetStdHandle(STD_INPUT_HANDLE);

    GetConsoleMode(ft_hout, &ft_orig_out_mode);
    GetConsoleMode(ft_hin,  &ft_orig_in_mode);

    /* Disable processed output so we control everything */
    SetConsoleMode(ft_hout, 0);

    /* Raw input: no line buffering, no echo, pass all keys through */
    SetConsoleMode(ft_hin,
        ENABLE_WINDOW_INPUT); /* catch resize events too */

    /* Remember the original text attributes for restore */
    GetConsoleScreenBufferInfo(ft_hout, &csbi);
    ft_orig_attrs = csbi.wAttributes;

    /* Normal = white on black, reverse = black on white */
    ft_normal_attr  = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    ft_reverse_attr = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;

    SetConsoleTextAttribute(ft_hout, ft_normal_attr);
    ft_out_pos = 0;

    platform_clear_screen();
    platform_flush();
}

void platform_shutdown(void)
{
    /* Restore original console modes and attributes */
    buf_flush();
    SetConsoleTextAttribute(ft_hout, ft_orig_attrs);
    SetConsoleMode(ft_hout, ft_orig_out_mode);
    SetConsoleMode(ft_hin,  ft_orig_in_mode);
}

void platform_get_size(int *rows, int *cols)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(ft_hout, &csbi)) {
        *cols = (int)(csbi.srWindow.Right  - csbi.srWindow.Left + 1);
        *rows = (int)(csbi.srWindow.Bottom - csbi.srWindow.Top  + 1);
    } else {
        *rows = 25;
        *cols = 80;
    }
}

void platform_move(int row, int col)
{
    COORD pos;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int   top;

    buf_flush(); /* SetConsoleCursorPosition needs a clean buffer state */

    GetConsoleScreenBufferInfo(ft_hout, &csbi);
    top = (int)csbi.srWindow.Top;

    pos.X = (SHORT)col;
    pos.Y = (SHORT)(top + row);
    SetConsoleCursorPosition(ft_hout, pos);
}

void platform_putch(int c)
{
    buf_putch((char)c);
}

void platform_puts(const char *s)
{
    buf_puts(s);
}

void platform_putn(const char *s, int n)
{
    buf_putn(s, n);
}

void platform_clear_eol(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD   pos;
    DWORD   written;
    int     cols_left;

    buf_flush();
    GetConsoleScreenBufferInfo(ft_hout, &csbi);
    pos       = csbi.dwCursorPosition;
    cols_left = (int)csbi.srWindow.Right - (int)pos.X + 1;

    if (cols_left > 0) {
        FillConsoleOutputCharacterA(ft_hout, ' ', (DWORD)cols_left, pos, &written);
        FillConsoleOutputAttribute(ft_hout, csbi.wAttributes,
                                   (DWORD)cols_left, pos, &written);
    }
}

void platform_clear_screen(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD   home;
    DWORD   cells, written;

    buf_flush();
    GetConsoleScreenBufferInfo(ft_hout, &csbi);
    home.X = csbi.srWindow.Left;
    home.Y = csbi.srWindow.Top;
    cells  = (DWORD)((csbi.srWindow.Right  - csbi.srWindow.Left + 1) *
                     (csbi.srWindow.Bottom - csbi.srWindow.Top  + 1));

    FillConsoleOutputCharacterA(ft_hout, ' ', cells, home, &written);
    FillConsoleOutputAttribute(ft_hout, ft_normal_attr, cells, home, &written);
    SetConsoleCursorPosition(ft_hout, home);
}

void platform_attr_reverse(void)
{
    buf_flush();
    SetConsoleTextAttribute(ft_hout, ft_reverse_attr);
}

void platform_attr_normal(void)
{
    buf_flush();
    SetConsoleTextAttribute(ft_hout, ft_normal_attr);
}

void platform_flush(void)
{
    buf_flush();
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
        if (!ke->bKeyDown)  continue;

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
