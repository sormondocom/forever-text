/*
 * editor.h - Core editor state and interface for Forever Text
 */

#ifndef FT_EDITOR_H
#define FT_EDITOR_H

#include "buffer.h"

#define FT_VERSION      "0.1"
#define FT_NAME         "Forever Text"
#define FT_TAB_SIZE     4

/* 6502 (cc65) targets have very limited RAM; clamp filename and message buffers. */
#ifdef __CC65__
#  define FT_MAX_FILENAME    16   /* C64 max=16, Atari max=12, Apple II max=15 */
#  define FT_STATUS_MSG_SIZE 80
#else
#  define FT_MAX_FILENAME    256
#  define FT_STATUS_MSG_SIZE 512
#endif

/* Number of screen rows consumed by the header (title bar + ruler) */
#define FT_HEADER_ROWS  2
/* Number of screen rows consumed by the footer (hotkey bar) */
#define FT_FOOTER_ROWS  1

/*
 * Hard page break: a buffer line whose sole content is this character.
 * Rendered as a visual separator; saved to file as a form-feed byte so
 * printers and converters honour it.  Soft page breaks (automatic every
 * paper_lines rows) are display-only and are never written to disk.
 */
#define FT_PGBREAK_CHAR '\014'  /* ASCII form-feed (^L) */

/* Standard paper sizes expressed as lines-per-page at 6 lpi */
#define FT_PAPER_NONE      0
#define FT_PAPER_A5       49   /* 148 x 210 mm */
#define FT_PAPER_EXECUTIVE 63   /* 7.25 x 10.5 in */
#define FT_PAPER_LETTER   66   /* 8.5 x 11 in */
#define FT_PAPER_A4       70   /* 210 x 297 mm */
#define FT_PAPER_LEGAL    84   /* 8.5 x 14 in */

/* Paper widths in columns (printable area at 10 CPI, standard margins) */
#define FT_PAPER_COLS_A5        56
#define FT_PAPER_COLS_EXECUTIVE 72
#define FT_PAPER_COLS_LETTER    80
#define FT_PAPER_COLS_A4        78
#define FT_PAPER_COLS_LEGAL     80

typedef struct {
    FtBuffer buf;

    int cursor_row;   /* 0-based row in the buffer */
    int cursor_col;   /* 0-based logical column in the buffer */

    int scroll_row;   /* first buffer row visible on screen */
    int scroll_col;   /* first buffer column visible on screen */

    int screen_rows;  /* total terminal rows */
    int screen_cols;  /* total terminal columns */

    char filename[FT_MAX_FILENAME];
    int  dirty;       /* 1 if there are unsaved changes */

    /* Single-line clipboard (Ctrl+K cut / Ctrl+U paste) */
    char *clipboard;
    int   clipboard_len;

    /*
     * Pagination.  0 = off.  Any positive value = lines per page.
     * Use the FT_PAPER_* constants for standard sizes, or any custom int.
     * Soft breaks are visual overlays; hard breaks are \f lines in the buffer.
     */
    int paper_lines;
    int paper_cols;   /* right-margin column count; 0 = off */
    int wrap;         /* 1 = auto word-wrap at paper_cols */

    int  running;                       /* main loop flag */
    char status_msg[FT_STATUS_MSG_SIZE]; /* transient message shown in footer */

    int indent_left;   /* blank columns before the text area (left margin) */
    int indent_right;  /* blank columns after the text area (right margin) */
    int indent_mode;   /* 1 while in border-adjust mode (^B) */
} FtEditor;

void ft_editor_init(FtEditor *ed);
void ft_editor_free(FtEditor *ed);

/* Load filename into the editor buffer */
void ft_editor_open(FtEditor *ed, const char *filename);

/* Save buffer to ed->filename.  Returns 1 on success. */
int  ft_editor_save(FtEditor *ed);

/* Run the main editor loop (blocks until the user quits) */
void ft_editor_run(FtEditor *ed);

#endif /* FT_EDITOR_H */
