/*
 * z80.c - Zilog Z80 / TRS-80 platform implementation for Forever Text
 *
 * Primary target: TRS-80 Model III (80x24 if available, else 64x16)
 * Secondary:      TRS-80 Model I   (64x16)
 *
 * Compiler: SDCC (Small Device C Compiler) — Z80 target
 *   sudo apt install sdcc
 *   sdcc --target z80 ...
 *
 * This platform layer writes directly to the TRS-80's memory-mapped
 * video RAM and reads the keyboard matrix through memory reads.
 * There is no OS terminal layer — we talk to the hardware directly.
 *
 * Video RAM layout:
 *   Model I / III (64-col mode): 0x3C00-0x3FFF  (64 x 16 = 1024 bytes)
 *   Model III / 4 (80-col mode): 0xF800-0xFFFF  (80 x 24 = 1920 bytes)
 *
 * Keyboard matrix:
 *   The TRS-80 keyboard is a 8x8 matrix read through memory addresses
 *   0x3800-0x38FF.  Each address in that range has bits set for keys
 *   that are currently pressed on one row of the matrix.
 *
 * Cursor:
 *   The TRS-80 has a hardware cursor that can be positioned by writing
 *   to address 0x37E0 (Model I) / 0x37E0 (Model III).  We use the
 *   software approach of saving and restoring the character underneath
 *   a cursor glyph for maximum portability across models.
 *
 * Memory note:
 *   Z80 address space is 64 KB.  The TRS-80 Model I has roughly 12-36 KB
 *   of user RAM depending on expansion.  Model III has up to 48 KB.
 *   Only small files will fit in memory.
 *
 * SDCC note:
 *   SDCC uses __at() to place variables at specific addresses and
 *   __sfr for I/O port SFRs.  Inline assembly uses __asm / __endasm.
 */

#include <stddef.h>
#include <string.h>

#include "platform.h"

/* ------------------------------------------------------------------ */
/* Model selection                                                       */
/*                                                                      */
/* Define TRS80_MODEL_III or TRS80_MODEL_I at compile time.            */
/* Defaults to Model III (64-column mode) if neither is defined.       */
/* ------------------------------------------------------------------ */

#if defined(TRS80_MODEL_I)
  #define VID_BASE   ((unsigned char __at(0x3C00) *)0x3C00)
  #define VID_COLS   64
  #define VID_ROWS   16
#elif defined(TRS80_MODEL_III_80)
  /* Model III / 4 in 80-column mode — requires hardware that supports it */
  #define VID_BASE   ((unsigned char __at(0xF800) *)0xF800)
  #define VID_COLS   80
  #define VID_ROWS   24
#else
  /* Model III default: 64x16 */
  #define VID_BASE   ((unsigned char __at(0x3C00) *)0x3C00)
  #define VID_COLS   64
  #define VID_ROWS   16
#endif

/* TRS-80 keyboard matrix base address */
#define KBD_BASE   ((volatile unsigned char __at(0x3800) *)0x3800)

/* Character used to indicate reverse video (TRS-80 block character) */
#define CHAR_BLOCK  0x80  /* First graphics block character in TRS-80 ROM */
#define CHAR_SPACE  0x20

/* ------------------------------------------------------------------ */
/* State                                                                 */
/* ------------------------------------------------------------------ */

static int   ft_cur_row;
static int   ft_cur_col;
static char  ft_under_cursor;    /* character saved beneath the cursor glyph */
static int   ft_cursor_visible;
static int   ft_reverse;         /* non-zero when reverse attribute active */

/* ------------------------------------------------------------------ */
/* Internal helpers                                                      */
/* ------------------------------------------------------------------ */

static void vid_write(int row, int col, char c)
{
    volatile unsigned char *vid = (volatile unsigned char *)VID_BASE;
    vid[row * VID_COLS + col] = (unsigned char)c;
}

static char vid_read(int row, int col)
{
    volatile unsigned char *vid = (volatile unsigned char *)VID_BASE;
    return (char)vid[row * VID_COLS + col];
}

static void hide_cursor(void)
{
    if (ft_cursor_visible) {
        vid_write(ft_cur_row, ft_cur_col, ft_under_cursor);
        ft_cursor_visible = 0;
    }
}

static void show_cursor(void)
{
    if (!ft_cursor_visible &&
        ft_cur_row >= 0 && ft_cur_row < VID_ROWS &&
        ft_cur_col >= 0 && ft_cur_col < VID_COLS) {
        ft_under_cursor = vid_read(ft_cur_row, ft_cur_col);
        vid_write(ft_cur_row, ft_cur_col, CHAR_BLOCK);
        ft_cursor_visible = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Public platform API                                                   */
/* ------------------------------------------------------------------ */

void platform_init(void)
{
    ft_cur_row       = 0;
    ft_cur_col       = 0;
    ft_cursor_visible = 0;
    ft_reverse       = 0;
    platform_clear_screen();
}

void platform_shutdown(void)
{
    hide_cursor();
    ft_reverse = 0;
    platform_move(VID_ROWS - 1, 0);
}

void platform_get_size(int *rows, int *cols)
{
    *rows = VID_ROWS;
    *cols = VID_COLS;
}

void platform_move(int row, int col)
{
    hide_cursor();
    ft_cur_row = row;
    ft_cur_col = col;
}

void platform_putch(int c)
{
    char ch;

    if (ft_cur_row < 0 || ft_cur_row >= VID_ROWS) return;
    if (ft_cur_col < 0 || ft_cur_col >= VID_COLS) return;

    /*
     * TRS-80 reverse video: the ROM character set has two halves.
     * Characters 0x00-0x3F are graphics; 0x40-0x7F are normal text.
     * "Reverse" is simulated by using the block/graphics range or
     * simply by XOR-ing bit 7 of the character code.
     *
     * We use a simple approach: OR the character with 0x80 to select
     * the alternate half of the character ROM (which displays inverted
     * on hardware that supports it, like the Model III).
     */
    ch = (char)c;
    if (ft_reverse && ch >= 0x20 && ch < 0x80)
        ch = (char)(ch | 0x80);

    vid_write(ft_cur_row, ft_cur_col, ch);
    ft_cur_col++;

    if (ft_cur_col >= VID_COLS) {
        ft_cur_col = 0;
        ft_cur_row++;
        if (ft_cur_row >= VID_ROWS)
            ft_cur_row = VID_ROWS - 1;
    }
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

void platform_clear_eol(void)
{
    int c;
    for (c = ft_cur_col; c < VID_COLS; c++)
        vid_write(ft_cur_row, c, CHAR_SPACE);
}

void platform_clear_screen(void)
{
    int r, c;
    hide_cursor();
    for (r = 0; r < VID_ROWS; r++)
        for (c = 0; c < VID_COLS; c++)
            vid_write(r, c, CHAR_SPACE);
    ft_cur_row = 0;
    ft_cur_col = 0;
}

void platform_attr_reverse(void)
{
    ft_reverse = 1;
}

void platform_attr_normal(void)
{
    ft_reverse = 0;
}

void platform_flush(void)
{
    /* Direct video RAM writes are immediate — show cursor at final position */
    show_cursor();
}

/* ------------------------------------------------------------------ */
/* Keyboard input                                                        */
/*                                                                      */
/* The TRS-80 keyboard matrix is read from 0x3800-0x38FF.              */
/* Each address selects one row; bits 0-7 indicate keys in that row.   */
/*                                                                      */
/* We poll until a key changes from not-pressed to pressed, then decode */
/* the row/column into a key code.  This is a basic polling loop;       */
/* a production implementation would use the Model III's interrupt-     */
/* driven keyboard routines via the ROM KBCHAR routine at 0x0049.      */
/* ------------------------------------------------------------------ */

/*
 * Read one character from the keyboard via the TRS-80 ROM routine.
 * The ROM KBCHAR entry point (Model III: 0x0049) waits for a keypress
 * and returns the ASCII code in register A (which maps to the return
 * value in SDCC's Z80 calling convention).
 *
 * Using the ROM routine is more portable across Model I/III variants
 * than direct matrix scanning.
 */
static int rom_getchar(void)
{
    /*
     * Call the TRS-80 ROM keyboard input routine.
     * Model I KBCHAR: 0x0049
     * Model III KBCHAR: 0x0049 (same address, different ROM)
     *
     * SDCC inline assembly: result returned in L register (low byte
     * of HL pair), which maps to the C int return value.
     */
    int result;
    __asm
        call 0x0049
        ld   l, a
        ld   h, #0
    __endasm;
    return result;
}

int platform_read_key(void)
{
    int c = rom_getchar();

    /*
     * TRS-80 ROM KBCHAR returns:
     *   0x08  Backspace (left arrow on Model I)
     *   0x0D  Enter
     *   0x1B  Escape (BREAK key on some models)
     *   0x09  Tab
     *   0x0A  Down arrow
     *   0x0B  Up arrow
     *   0x0C  Right arrow (some models)
     *   0x08  Left arrow / Backspace
     *   0x20-0x7E  Printable ASCII
     *   0x01-0x1A  Ctrl+A through Ctrl+Z
     *
     * Shift arrow keys (Model III with SHIFT held):
     *   Up: 0x5B, Down: 0x5C, Right: 0x5D, Left: 0x5E
     * These are not universally available — use unshifted keys for
     * cursor movement on machines without dedicated cursor keys.
     */

    switch (c) {
        case 0x0B: return KEY_UP;
        case 0x0A: return KEY_DOWN;
        case 0x0C: return KEY_RIGHT;
        case 0x08: return KEY_LEFT;   /* also Backspace — handled below */
        case 0x0D: return KEY_ENTER;
        case 0x1B: return KEY_ESCAPE;
        case 0x7F: return KEY_DELETE;
        default:   break;
    }

    /*
     * If 0x08 appears as a standalone backspace (not cursor-left),
     * we cannot distinguish without examining shift state via the
     * keyboard matrix.  We return KEY_BACKSPACE and let the editor
     * handle it uniformly.
     */
    if (c == 0x08) return KEY_BACKSPACE;

    /* Ctrl+letter */
    if (c >= 1 && c <= 26) return c;

    /* Printable ASCII */
    if (c >= 0x20 && c < 0x7F) return c;

    return KEY_UNKNOWN;
}
