/*
 * platform.h - Terminal abstraction layer for Forever Text
 *
 * Each target provides one implementation of these functions:
 *   ansi.c  - ANSI/VT100 terminals (Unix, VAX/VMS, DOS+ANSI.SYS, macOS)
 *   win32.c - Windows Console API (Win32, modern Windows native)
 *   bios.c  - DOS BIOS direct via INT 10h/16h (no ANSI.SYS required)
 */

#ifndef FT_PLATFORM_H
#define FT_PLATFORM_H

/* Initialize terminal for editor use (raw mode, no echo, etc.) */
void platform_init(void);

/* Restore terminal to its original state before exit */
void platform_shutdown(void);

/* Return usable terminal dimensions via output parameters */
void platform_get_size(int *rows, int *cols);

/* Move cursor to 0-based row and column */
void platform_move(int row, int col);

/* Write a single character at the current cursor position */
void platform_putch(int c);

/* Write a null-terminated string at the current cursor position */
void platform_puts(const char *s);

/* Write exactly n characters from s */
void platform_putn(const char *s, int n);

/* Clear from current cursor position to end of current line */
void platform_clear_eol(void);

/* Clear the entire screen */
void platform_clear_screen(void);

/* Enable reverse video attribute (used for header and footer bars) */
void platform_attr_reverse(void);

/* Return to normal video attribute */
void platform_attr_normal(void);

/* Flush all buffered output to the terminal */
void platform_flush(void);

/*
 * Block until a key is pressed and return a KEY_* constant below.
 * Printable ASCII characters are returned as their ASCII value.
 * Returns KEY_UNKNOWN for unrecognised sequences.
 */
int platform_read_key(void);

/* --- Key constants --- */

/* Unrecognised input */
#define KEY_UNKNOWN    (-1)

/* Control characters (ASCII 1-31) */
#define KEY_CTRL_A      1
#define KEY_CTRL_B      2
#define KEY_CTRL_C      3
#define KEY_CTRL_D      4
#define KEY_CTRL_E      5
#define KEY_CTRL_F      6
#define KEY_CTRL_G      7
#define KEY_BACKSPACE   8   /* also Ctrl+H */
#define KEY_TAB         9
#define KEY_CTRL_J      10
#define KEY_CTRL_K      11
#define KEY_CTRL_L      12
#define KEY_ENTER       13  /* Ctrl+M */
#define KEY_CTRL_N      14
#define KEY_CTRL_O      15
#define KEY_CTRL_P      16
#define KEY_CTRL_Q      17
#define KEY_CTRL_R      18
#define KEY_CTRL_S      19
#define KEY_CTRL_T      20
#define KEY_CTRL_U      21
#define KEY_CTRL_V      22
#define KEY_CTRL_W      23
#define KEY_CTRL_X      24
#define KEY_CTRL_Y      25
#define KEY_CTRL_Z      26
#define KEY_ESCAPE      27
#define KEY_DELETE      127

/* Extended keys encoded above the printable ASCII range */
#define KEY_UP          256
#define KEY_DOWN        257
#define KEY_LEFT        258
#define KEY_RIGHT       259
#define KEY_PAGE_UP     260
#define KEY_PAGE_DOWN   261
#define KEY_HOME        262
#define KEY_END         263
#define KEY_F1          264
#define KEY_F2          265
#define KEY_F3          266
#define KEY_F4          267
#define KEY_F5          268
#define KEY_F6          269
#define KEY_F7          270
#define KEY_F8          271
#define KEY_F9          272
#define KEY_F10         273

#endif /* FT_PLATFORM_H */
