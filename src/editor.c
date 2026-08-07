/*
 * editor.c - Core editor logic for Forever Text
 *
 * Rendering model:
 *   Row 0:        Title bar (reverse video)   -- filename, position, dirty flag
 *   Row 1:        Column ruler                 -- typewriter-style position guide
 *   Rows 2..N-2:  Editing area
 *   Row N-1:      Hotkey bar (reverse video)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "platform/platform.h"

/* ------------------------------------------------------------------ */
/* Internal helpers                                                      */
/* ------------------------------------------------------------------ */

static void clamp_cursor(FtEditor *ed)
{
    int line_len;

    if (ed->cursor_row < 0)
        ed->cursor_row = 0;
    if (ed->cursor_row >= ed->buf.num_lines)
        ed->cursor_row = ed->buf.num_lines - 1;

    line_len = ed->buf.lines[ed->cursor_row].len;
    if (ed->cursor_col < 0)
        ed->cursor_col = 0;
    if (ed->cursor_col > line_len)
        ed->cursor_col = line_len;
}

static void scroll_to_cursor(FtEditor *ed)
{
    int edit_rows;
    int edit_cols;

    edit_rows = ed->screen_rows - FT_HEADER_ROWS - FT_FOOTER_ROWS;
    edit_cols = ed->screen_cols;

    /* Vertical scroll */
    if (ed->cursor_row < ed->scroll_row)
        ed->scroll_row = ed->cursor_row;
    if (ed->cursor_row >= ed->scroll_row + edit_rows)
        ed->scroll_row = ed->cursor_row - edit_rows + 1;

    /* Horizontal scroll */
    if (ed->cursor_col < ed->scroll_col)
        ed->scroll_col = ed->cursor_col;
    if (ed->cursor_col >= ed->scroll_col + edit_cols)
        ed->scroll_col = ed->cursor_col - edit_cols + 1;
}

/* Write exactly n spaces */
static void put_spaces(int n)
{
    int i;
    for (i = 0; i < n; i++)
        platform_putch(' ');
}

/* Return a display name for a paper_lines value */
static const char *paper_name(int lines)
{
    switch (lines) {
        case FT_PAPER_A5:        return "A5";
        case FT_PAPER_EXECUTIVE: return "Executive";
        case FT_PAPER_LETTER:    return "Letter";
        case FT_PAPER_A4:        return "A4";
        case FT_PAPER_LEGAL:     return "Legal";
        default:                 return "Custom";
    }
}

/*
 * Return 1 if buffer line ln is a hard page-break marker.
 * A hard page break is stored as a single form-feed byte.
 */
static int is_pgbreak(FtLine *ln)
{
    return (ln->len == 1 && (unsigned char)ln->data[0] == FT_PGBREAK_CHAR);
}

/* ------------------------------------------------------------------ */
/* Screen drawing                                                        */
/* ------------------------------------------------------------------ */

/*
 * Draw the title bar (row 0).
 * Shows: editor name | filename | line/col | modified flag
 */
static void draw_title(FtEditor *ed)
{
    static char  info[96];
    static char  name_field[FT_MAX_FILENAME + 32];
    int   name_len;
    int   info_len;
    int   pad;
    const char *fname;
    int   col_display;

    platform_move(0, 0);
    platform_attr_reverse();

    fname = (ed->filename[0]) ? ed->filename : "[No Name]";

    /* Right-hand status block includes optional pagination info */
    col_display = ed->cursor_col + 1;

    if (ed->paper_lines > 0) {
        int cur_page  = ed->cursor_row / ed->paper_lines + 1;
        int tot_pages = (ed->buf.num_lines + ed->paper_lines - 1)
                        / ed->paper_lines;
        sprintf(info, "  %s Pg %d/%d  Ln:%4d  Col:%3d  %s  ",
                paper_name(ed->paper_lines),
                cur_page, tot_pages,
                ed->cursor_row + 1, col_display,
                ed->dirty ? "[Modified]" : "[Clean]  ");
    } else {
        sprintf(info, "  Ln:%4d  Col:%3d  %s  ",
                ed->cursor_row + 1,
                col_display,
                ed->dirty ? "[Modified]" : "[Clean]  ");
    }

    sprintf(name_field, "  " FT_NAME " v" FT_VERSION "  |  %s", fname);

    name_len = (int)strlen(name_field);
    info_len = (int)strlen(info);

    /* Print left part */
    if (name_len > ed->screen_cols - info_len)
        name_len = ed->screen_cols - info_len;
    platform_putn(name_field, name_len);

    /* Pad the middle */
    pad = ed->screen_cols - name_len - info_len;
    if (pad > 0)
        put_spaces(pad);

    /* Print right part */
    platform_puts(info);

    platform_attr_normal();
}

/*
 * Draw the column ruler (row 1).
 * Classic typewriter guide: shows every 10th column, highlights
 * the column under the cursor with a '^' marker.
 *
 * When scrolled horizontally the ruler tracks the scroll offset so
 * the displayed numbers always reflect the actual buffer column.
 */
static void draw_ruler(FtEditor *ed)
{
    int   col;
    int   buf_col;
    char  c;
    int   cursor_screen_col;
    int   margin_screen_col;

    platform_move(1, 0);

    cursor_screen_col = ed->cursor_col - ed->scroll_col;
    margin_screen_col = (ed->paper_cols > 0)
                        ? ed->paper_cols - ed->scroll_col
                        : -1;

    for (col = 0; col < ed->screen_cols; col++) {
        buf_col = col + ed->scroll_col + 1;  /* 1-based for display */

        if (col == cursor_screen_col) {
            platform_putch('v');
        } else if (col == margin_screen_col) {
            platform_attr_reverse();
            platform_putch('|');
            platform_attr_normal();
        } else if (buf_col % 10 == 0) {
            c = (char)('0' + (buf_col / 10) % 10);
            platform_putch(c);
        } else if (buf_col % 5 == 0) {
            platform_putch('+');
        } else {
            platform_putch('-');
        }
    }
}

/*
 * Draw a soft page-break separator (automatic, display-only).
 * Rendered in reverse video with dash wings around a centred label so it
 * stands out clearly like the hard page-break separator.
 * Never written to the file — purely a visual cue.
 */
static void draw_soft_sep(FtEditor *ed, int display_row, int page_num)
{
    static char label[48];
    int  tot_pages = (ed->buf.num_lines + ed->paper_lines - 1) / ed->paper_lines;
    int  label_len, left, right, i;

    if (tot_pages < 1) tot_pages = 1;
    sprintf(label, "  end of page %d of %d  ", page_num, tot_pages);
    label_len = (int)strlen(label);
    left      = (ed->screen_cols - label_len) / 2;
    right     = ed->screen_cols - label_len - left;
    if (left  < 0) left  = 0;
    if (right < 0) right = 0;

    platform_move(FT_HEADER_ROWS + display_row, 0);
    platform_attr_reverse();
    for (i = 0; i < left;  i++) platform_putch('-');
    platform_putn(label, label_len);
    for (i = 0; i < right; i++) platform_putch('-');
    platform_attr_normal();
}

/*
 * Draw a hard page-break separator (user-inserted, stored as \f in buffer).
 * Rendered in reverse video with "= [PAGE BREAK] =" wings.
 */
static void draw_hard_sep(FtEditor *ed, int display_row)
{
    const char *label    = " [PAGE BREAK] ";
    int         llen     = (int)strlen(label);
    int         left     = (ed->screen_cols - llen) / 2;
    int         right    = ed->screen_cols - llen - left;
    int         i;

    platform_move(FT_HEADER_ROWS + display_row, 0);
    platform_attr_reverse();
    for (i = 0; i < left;  i++) platform_putch('=');
    platform_putn(label, llen);
    for (i = 0; i < right; i++) platform_putch('=');
    platform_attr_normal();
}

/*
 * Draw the right-margin indicator (a single reverse-video '|') at the
 * paper_cols screen column for the given editing display row.
 * No-op when paper_cols is 0 or the column is outside the visible area.
 */
static void draw_margin(FtEditor *ed, int display_row)
{
    int margin_screen_col;
    if (ed->paper_cols <= 0) return;
    margin_screen_col = ed->paper_cols - ed->scroll_col;
    if (margin_screen_col < 0 || margin_screen_col >= ed->screen_cols) return;
    platform_move(FT_HEADER_ROWS + display_row, margin_screen_col);
    platform_attr_reverse();
    platform_putch('|');
    platform_attr_normal();
}

/*
 * Draw the editing area (rows FT_HEADER_ROWS to screen_rows-2).
 *
 * Soft page-break separators are injected between buffer rows whenever
 * a row index is an exact multiple of paper_lines.  They consume one
 * display row each but correspond to no buffer position, so buf_row is
 * NOT advanced after drawing them.
 *
 * Hard page-break lines (\f sole content) are displayed as a separator
 * and DO advance buf_row, since they occupy one real buffer slot.
 *
 * sep_drawn_for guards against re-drawing the same soft separator when
 * the loop re-tests the same buf_row after a continue.
 */
static void draw_rows(FtEditor *ed)
{
    int    display_row   = 0;
    int    buf_row       = ed->scroll_row;
    int    edit_rows     = ed->screen_rows - FT_HEADER_ROWS - FT_FOOTER_ROWS;
    int    sep_drawn_for = -1;
    /*
     * eof_sep_shown: once we draw a soft separator whose buf_row is past the
     * real buffer end (a "projected" separator for a short document), we stop
     * projecting further ones so the screen doesn't fill with consecutive
     * separator bars.
     */
    int    eof_sep_shown = 0;
    FtLine *ln;
    int    line_len, visible_len;

    while (display_row < edit_rows) {

        /* --- Inject a soft separator at each page boundary ---
         * Fires for real content boundaries AND for projected ones (when buf_row
         * was jumped to the separator position by the EOF branch below).
         * Suppressed once eof_sep_shown is set so only one phantom separator
         * appears for documents shorter than one page.                       */
        if (ed->paper_lines > 0 &&
            buf_row > 0 &&
            buf_row % ed->paper_lines == 0 &&
            sep_drawn_for != buf_row &&
            !eof_sep_shown) {

            sep_drawn_for = buf_row;
            draw_soft_sep(ed, display_row, buf_row / ed->paper_lines);
            display_row++;
            /* Mark as EOF separator so no further phantom bars are drawn */
            if (buf_row >= ed->buf.num_lines)
                eof_sep_shown = 1;
            /* buf_row unchanged: next iteration draws the real line (or clears) */
            continue;
        }

        /* --- Past end of buffer --- */
        if (buf_row >= ed->buf.num_lines) {
            /*
             * If pagination is on and no phantom separator has been shown yet,
             * check whether the next page boundary is too far to reach through
             * normal one-row-at-a-time iteration (which is limited to edit_rows
             * total display rows).  When it is, jump buf_row directly to the
             * boundary so the separator check fires on the very next iteration,
             * making the page edge visible even for documents shorter than one
             * page of paper.
             */
            if (ed->paper_lines > 0 && !eof_sep_shown) {
                int next_sep = ((buf_row / ed->paper_lines) + 1) * ed->paper_lines;
                int distance = next_sep - buf_row;
                /* "distance >= remaining rows" means the separator falls on or
                 * past the last available display row — it will never be reached
                 * naturally, so jump.                                          */
                if (distance >= edit_rows - display_row &&
                    sep_drawn_for != next_sep) {
                    buf_row = next_sep;
                    continue; /* separator check fires at top of next iteration */
                }
            }
            platform_move(FT_HEADER_ROWS + display_row, 0);
            platform_clear_eol();
            draw_margin(ed, display_row);
            buf_row++;
            display_row++;
            continue;
        }

        /* --- Draw the buffer line --- */
        platform_move(FT_HEADER_ROWS + display_row, 0);
        ln = &ed->buf.lines[buf_row];

        if (is_pgbreak(ln)) {
            /* Hard page break: show separator, consume the buffer line */
            draw_hard_sep(ed, display_row);
        } else {
            line_len = ln->len;

            if (ed->scroll_col >= line_len) {
                platform_clear_eol();
            } else {
                visible_len = line_len - ed->scroll_col;
                if (visible_len > ed->screen_cols)
                    visible_len = ed->screen_cols;
                platform_putn(ln->data + ed->scroll_col, visible_len);
                if (visible_len < ed->screen_cols)
                    put_spaces(ed->screen_cols - visible_len);
            }
            draw_margin(ed, display_row);
        }

        buf_row++;
        display_row++;
    }
}

/*
 * Draw the footer hotkey bar (last row).
 * Shows the primary key bindings in reverse video.
 */
static void draw_footer(FtEditor *ed)
{
    static char hotkeys[256];
    const char *wrap_indicator;
    int         hlen;
    int         pad;

    if (ed->paper_cols > 0)
        wrap_indicator = ed->wrap ? "[WRAP]" : "[----]";
    else
        wrap_indicator = "      ";

    sprintf(hotkeys,
        " ^S Save  ^L Load  ^Q Quit  ^K Cut  ^U Paste"
        "  ^F Find  ^G Goto  ^P PgBrk  ^O Paper  ^W %s",
        wrap_indicator);

    platform_move(ed->screen_rows - 1, 0);
    platform_attr_reverse();

    /* If there is a status message, show it instead */
    if (ed->status_msg[0]) {
        int mlen = (int)strlen(ed->status_msg);
        if (mlen > ed->screen_cols) mlen = ed->screen_cols;
        platform_putn(ed->status_msg, mlen);
        pad = ed->screen_cols - mlen;
        if (pad > 0) put_spaces(pad);
        ed->status_msg[0] = '\0';  /* consume after one frame */
    } else {
        hlen = (int)strlen(hotkeys);
        if (hlen > ed->screen_cols) hlen = ed->screen_cols;
        platform_putn(hotkeys, hlen);
        pad = ed->screen_cols - hlen;
        if (pad > 0) put_spaces(pad);
    }

    platform_attr_normal();
}

/*
 * Full screen redraw.  We hide the cursor, draw everything, then
 * reposition it and show it again to avoid any visible flickering.
 */
static void draw_screen(FtEditor *ed)
{
    int screen_cursor_row;
    int screen_cursor_col;

    /* Re-query size in case the terminal was resized */
    platform_get_size(&ed->screen_rows, &ed->screen_cols);

    scroll_to_cursor(ed);

    draw_title(ed);
    draw_ruler(ed);
    draw_rows(ed);
    draw_footer(ed);

    /* Position the hardware cursor inside the editing area */
    screen_cursor_row = FT_HEADER_ROWS + (ed->cursor_row - ed->scroll_row);
    screen_cursor_col = ed->cursor_col - ed->scroll_col;

    /*
     * Soft page-break separator lines are injected into the display between
     * buf_row N-1 and N (where N % paper_lines == 0).  Each one shifts the
     * cursor down by one terminal row, so we count them here.
     */
    if (ed->paper_lines > 0) {
        int r;
        for (r = ed->scroll_row + 1; r <= ed->cursor_row; r++) {
            if (r % ed->paper_lines == 0)
                screen_cursor_row++;
        }
        /* Clamp: never land on the footer row */
        if (screen_cursor_row >= ed->screen_rows - FT_FOOTER_ROWS)
            screen_cursor_row  = ed->screen_rows - FT_FOOTER_ROWS - 1;
    }

    if (screen_cursor_col < 0) screen_cursor_col = 0;

    platform_move(screen_cursor_row, screen_cursor_col);
    platform_flush();
}

/* ------------------------------------------------------------------ */
/* Prompt (reads a string from the footer area)                         */
/* ------------------------------------------------------------------ */

/*
 * Display prompt_text in the footer and read a line of input.
 * Returns the number of characters read, or -1 if the user pressed Escape.
 */
static int ft_prompt(FtEditor *ed, const char *prompt_text,
                     char *result, int max_len)
{
    int  len = 0;
    int  key;
    int  prompt_len;
    int  footer_row;

    result[0]  = '\0';
    prompt_len = (int)strlen(prompt_text);
    footer_row = ed->screen_rows - 1;

    for (;;) {
        /* Render prompt + current input */
        platform_move(footer_row, 0);
        platform_attr_reverse();
        platform_puts(prompt_text);
        platform_putn(result, len);
        platform_clear_eol();
        platform_attr_normal();
        platform_move(footer_row, prompt_len + len);
        platform_flush();

        key = platform_read_key();

        if (key == KEY_ESCAPE) {
            result[0] = '\0';
            return -1;
        }
        if (key == KEY_ENTER) {
            result[len] = '\0';
            return len;
        }
        if ((key == KEY_BACKSPACE || key == KEY_DELETE) && len > 0) {
            len--;
            result[len] = '\0';
            continue;
        }
        if (key >= 32 && key < 127 && len < max_len - 1) {
            result[len++] = (char)key;
            result[len]   = '\0';
        }
    }
}

/* ------------------------------------------------------------------ */
/* Commands                                                              */
/* ------------------------------------------------------------------ */

static void cmd_save(FtEditor *ed)
{
    static char fname[FT_MAX_FILENAME];

    if (!ed->filename[0]) {
        if (ft_prompt(ed, "Save as: ", fname, FT_MAX_FILENAME) <= 0)
            return;
        strncpy(ed->filename, fname, FT_MAX_FILENAME - 1);
        ed->filename[FT_MAX_FILENAME - 1] = '\0';
    }

    if (ft_buffer_save(&ed->buf, ed->filename)) {
        ed->dirty = 0;
        sprintf(ed->status_msg, "Saved: %s", ed->filename);
    } else {
        sprintf(ed->status_msg, "ERROR: Could not save %s", ed->filename);
    }
}

static void cmd_load(FtEditor *ed)
{
    static char fname[FT_MAX_FILENAME];

    if (ft_prompt(ed, "Open file: ", fname, FT_MAX_FILENAME) <= 0)
        return;

    if (ft_buffer_load(&ed->buf, fname)) {
        strncpy(ed->filename, fname, FT_MAX_FILENAME - 1);
        ed->filename[FT_MAX_FILENAME - 1] = '\0';
        ed->cursor_row = 0;
        ed->cursor_col = 0;
        ed->scroll_row = 0;
        ed->scroll_col = 0;
        ed->dirty      = 0;
        sprintf(ed->status_msg, "Opened: %s (%d lines)",
                fname, ed->buf.num_lines);
    } else {
        sprintf(ed->status_msg, "ERROR: Could not open %s", fname);
    }
}

static void cmd_quit(FtEditor *ed)
{
    char ans[8];

    if (ed->dirty) {
        if (ft_prompt(ed, "Unsaved changes. Quit anyway? (y/n): ",
                      ans, sizeof(ans)) < 0)
            return;
        if (ans[0] != 'y' && ans[0] != 'Y')
            return;
    }
    ed->running = 0;
}

static void cmd_cut_line(FtEditor *ed)
{
    FtLine *ln;

    ln = &ed->buf.lines[ed->cursor_row];

    /* Copy the line to clipboard */
    free(ed->clipboard);
    ed->clipboard     = (char *)malloc((size_t)ln->len + 1);
    ed->clipboard_len = 0;

    if (ed->clipboard) {
        memcpy(ed->clipboard, ln->data, (size_t)ln->len + 1);
        ed->clipboard_len = ln->len;
    }

    ft_buffer_delete_line(&ed->buf, ed->cursor_row);
    clamp_cursor(ed);
    ed->dirty = 1;
    sprintf(ed->status_msg, "Line cut to clipboard");
}

static void cmd_paste(FtEditor *ed)
{
    int i;

    if (!ed->clipboard || ed->clipboard_len == 0) {
        sprintf(ed->status_msg, "Clipboard is empty");
        return;
    }

    /* Insert a new line above the cursor and fill it */
    ft_buffer_insert_line(&ed->buf, ed->cursor_row);
    for (i = 0; i < ed->clipboard_len; i++)
        ft_buffer_insert_char(&ed->buf, ed->cursor_row, i, ed->clipboard[i]);

    ed->dirty = 1;
    sprintf(ed->status_msg, "Pasted %d chars", ed->clipboard_len);
}

static void cmd_find(FtEditor *ed)
{
    static char needle[256];
    int    start_row;
    int    r;
    char  *p;
    int    found_row;
    int    found_col;

    if (ft_prompt(ed, "Find: ", needle, sizeof(needle)) <= 0)
        return;
    if (!needle[0])
        return;

    /* Simple forward linear search from the line after cursor */
    found_row = -1;
    found_col = -1;
    start_row = ed->cursor_row;

    for (r = 0; r < ed->buf.num_lines; r++) {
        int row = (start_row + r) % ed->buf.num_lines;
        p = strstr(ed->buf.lines[row].data, needle);
        if (p) {
            found_row = row;
            found_col = (int)(p - ed->buf.lines[row].data);
            break;
        }
    }

    if (found_row >= 0) {
        ed->cursor_row = found_row;
        ed->cursor_col = found_col;
        sprintf(ed->status_msg, "Found at line %d", found_row + 1);
    } else {
        sprintf(ed->status_msg, "Not found: %s", needle);
    }
}

static void cmd_goto_line(FtEditor *ed)
{
    char  num_str[16];
    int   target;

    if (ft_prompt(ed, "Goto line: ", num_str, sizeof(num_str)) <= 0)
        return;

    target = 0;
    {
        int i;
        for (i = 0; num_str[i] >= '0' && num_str[i] <= '9'; i++)
            target = target * 10 + (num_str[i] - '0');
    }

    if (target < 1) target = 1;
    if (target > ed->buf.num_lines) target = ed->buf.num_lines;

    ed->cursor_row = target - 1;
    ed->cursor_col = 0;
    sprintf(ed->status_msg, "Jumped to line %d", target);
}

/*
 * Set the paper size interactively.
 * Accepts: none / letter / legal / a4 / a5 / exec  or  a plain integer
 * (lines per page) for arbitrary custom sizes.
 */
static void cmd_paper_size(FtEditor *ed)
{
    char buf[32];
    int  n, i;

    if (ft_prompt(ed,
                  "Paper (none/letter/legal/a4/a5/exec/NNN lines): ",
                  buf, sizeof(buf)) < 0 || !buf[0])
        return;

    if (strcmp(buf, "none") == 0 || strcmp(buf, "0") == 0) {
        ed->paper_lines = FT_PAPER_NONE;
        ed->paper_cols  = 0;
        ed->wrap        = 0;
        sprintf(ed->status_msg, "Pagination off");

    } else if (strcmp(buf, "letter") == 0) {
        ed->paper_lines = FT_PAPER_LETTER;
        ed->paper_cols  = FT_PAPER_COLS_LETTER;
        ed->wrap        = 1;
        sprintf(ed->status_msg, "Letter: %d lines/page", FT_PAPER_LETTER);

    } else if (strcmp(buf, "legal") == 0) {
        ed->paper_lines = FT_PAPER_LEGAL;
        ed->paper_cols  = FT_PAPER_COLS_LEGAL;
        ed->wrap        = 1;
        sprintf(ed->status_msg, "Legal: %d lines/page", FT_PAPER_LEGAL);

    } else if (strcmp(buf, "a4") == 0) {
        ed->paper_lines = FT_PAPER_A4;
        ed->paper_cols  = FT_PAPER_COLS_A4;
        ed->wrap        = 1;
        sprintf(ed->status_msg, "A4: %d lines/page", FT_PAPER_A4);

    } else if (strcmp(buf, "a5") == 0) {
        ed->paper_lines = FT_PAPER_A5;
        ed->paper_cols  = FT_PAPER_COLS_A5;
        ed->wrap        = 1;
        sprintf(ed->status_msg, "A5: %d lines/page", FT_PAPER_A5);

    } else if (strcmp(buf, "exec") == 0 || strcmp(buf, "executive") == 0) {
        ed->paper_lines = FT_PAPER_EXECUTIVE;
        ed->paper_cols  = FT_PAPER_COLS_EXECUTIVE;
        ed->wrap        = 1;
        sprintf(ed->status_msg, "Executive: %d lines/page", FT_PAPER_EXECUTIVE);

    } else {
        /* Try to parse as a plain line count */
        n = 0;
        for (i = 0; buf[i] >= '0' && buf[i] <= '9'; i++)
            n = n * 10 + (buf[i] - '0');

        if (n > 0) {
            ed->paper_lines = n;
            ed->paper_cols  = FT_PAPER_COLS_LETTER;  /* default width */
            ed->wrap        = 1;
            sprintf(ed->status_msg, "Custom: %d lines/page", n);
        } else {
            sprintf(ed->status_msg,
                    "Unknown size -- try: letter/legal/a4/a5/exec or a number");
        }
    }
}

/*
 * Insert a hard page break (form-feed line) before the current line.
 * The break is stored in the buffer as a single \f byte and is written
 * to disk verbatim; printers and formatters that understand ^L will
 * honour it.  On screen it appears as a reverse-video "==[PAGE BREAK]=="
 * separator that cannot be typed into.
 */
static void cmd_insert_page_break(FtEditor *ed)
{
    ft_buffer_insert_line(&ed->buf, ed->cursor_row);
    ft_buffer_insert_char(&ed->buf, ed->cursor_row, 0, FT_PGBREAK_CHAR);
    ed->cursor_row++;   /* move back onto the original content line */
    ed->dirty = 1;
    sprintf(ed->status_msg, "Page break inserted (^L in file)");
}

static void cmd_toggle_wrap(FtEditor *ed)
{
    if (ed->paper_cols <= 0) {
        sprintf(ed->status_msg, "No paper width -- set paper first (^O)");
        return;
    }
    ed->wrap ^= 1;
    sprintf(ed->status_msg, "Word wrap %s", ed->wrap ? "on" : "off");
}

/*
 * If wrap is on and cursor_col >= paper_cols, find the last space at or
 * before the margin and break there.  Falls back to a hard split at
 * paper_cols when no space is found.
 */
static void maybe_wrap(FtEditor *ed)
{
    int    break_col, i, new_col;
    FtLine *ln;

    if (!ed->wrap || ed->paper_cols <= 0) return;
    if (ed->cursor_col < ed->paper_cols) return;

    ln = &ed->buf.lines[ed->cursor_row];

    /* Scan backwards for a space at or before the margin */
    break_col = -1;
    for (i = ed->paper_cols - 1; i >= 0; i--) {
        if (i < ln->len && (unsigned char)ln->data[i] == ' ') {
            break_col = i;
            break;
        }
    }

    if (break_col >= 0) {
        /* Soft wrap: remove the space and split there */
        new_col = ed->cursor_col - break_col - 1;
        ft_buffer_delete_char(&ed->buf, ed->cursor_row, break_col);
        ft_buffer_split_line(&ed->buf, ed->cursor_row, break_col);
        ed->cursor_row++;
        ed->cursor_col = new_col;
    } else {
        /* Hard wrap: split exactly at the margin */
        new_col = ed->cursor_col - ed->paper_cols;
        ft_buffer_split_line(&ed->buf, ed->cursor_row, ed->paper_cols);
        ed->cursor_row++;
        ed->cursor_col = new_col;
    }
}

/* ------------------------------------------------------------------ */
/* Key dispatch                                                          */
/* ------------------------------------------------------------------ */

static void process_key(FtEditor *ed, int key)
{
    int line_len;

    switch (key) {
        /* --- Navigation --- */
        case KEY_UP:
            if (ed->cursor_row > 0)
                ed->cursor_row--;
            clamp_cursor(ed);
            break;

        case KEY_DOWN:
            if (ed->cursor_row < ed->buf.num_lines - 1)
                ed->cursor_row++;
            clamp_cursor(ed);
            break;

        case KEY_LEFT:
            if (ed->cursor_col > 0) {
                ed->cursor_col--;
            } else if (ed->cursor_row > 0) {
                ed->cursor_row--;
                ed->cursor_col = ed->buf.lines[ed->cursor_row].len;
            }
            break;

        case KEY_RIGHT:
            line_len = ed->buf.lines[ed->cursor_row].len;
            if (ed->cursor_col < line_len) {
                ed->cursor_col++;
            } else if (ed->cursor_row < ed->buf.num_lines - 1) {
                ed->cursor_row++;
                ed->cursor_col = 0;
            }
            break;

        case KEY_HOME:
            ed->cursor_col = 0;
            break;

        case KEY_END:
            ed->cursor_col = ed->buf.lines[ed->cursor_row].len;
            break;

        case KEY_PAGE_UP:
            {
                int edit_rows = ed->screen_rows - FT_HEADER_ROWS - FT_FOOTER_ROWS;
                ed->cursor_row -= edit_rows;
                if (ed->cursor_row < 0) ed->cursor_row = 0;
                clamp_cursor(ed);
            }
            break;

        case KEY_PAGE_DOWN:
            {
                int edit_rows = ed->screen_rows - FT_HEADER_ROWS - FT_FOOTER_ROWS;
                ed->cursor_row += edit_rows;
                if (ed->cursor_row >= ed->buf.num_lines)
                    ed->cursor_row = ed->buf.num_lines - 1;
                clamp_cursor(ed);
            }
            break;

        /* --- Editing --- */
        case KEY_ENTER:
            ft_buffer_split_line(&ed->buf, ed->cursor_row, ed->cursor_col);
            ed->cursor_row++;
            ed->cursor_col = 0;
            ed->dirty = 1;
            break;

        case KEY_BACKSPACE:
            if (ed->cursor_col > 0) {
                ed->cursor_col--;
                ft_buffer_delete_char(&ed->buf, ed->cursor_row, ed->cursor_col);
            } else if (ed->cursor_row > 0) {
                int prev_len = ed->buf.lines[ed->cursor_row - 1].len;
                ft_buffer_join_lines(&ed->buf, ed->cursor_row - 1);
                ed->cursor_row--;
                ed->cursor_col = prev_len;
            }
            ed->dirty = 1;
            break;

        case KEY_DELETE:
            line_len = ed->buf.lines[ed->cursor_row].len;
            if (ed->cursor_col < line_len) {
                ft_buffer_delete_char(&ed->buf, ed->cursor_row, ed->cursor_col);
            } else if (ed->cursor_row < ed->buf.num_lines - 1) {
                ft_buffer_join_lines(&ed->buf, ed->cursor_row);
            }
            ed->dirty = 1;
            break;

        case KEY_TAB:
            {
                int i;
                int spaces = FT_TAB_SIZE - (ed->cursor_col % FT_TAB_SIZE);
                for (i = 0; i < spaces; i++) {
                    ft_buffer_insert_char(&ed->buf, ed->cursor_row,
                                          ed->cursor_col, ' ');
                    ed->cursor_col++;
                }
                ed->dirty = 1;
            }
            break;

        /* --- Commands --- */
        case KEY_CTRL_S:
            cmd_save(ed);
            break;

        case KEY_CTRL_L:
            cmd_load(ed);
            break;

        case KEY_CTRL_Q:
            cmd_quit(ed);
            break;

        case KEY_CTRL_K:
            cmd_cut_line(ed);
            break;

        case KEY_CTRL_U:
            cmd_paste(ed);
            break;

        case KEY_CTRL_F:
            cmd_find(ed);
            break;

        case KEY_CTRL_G:
            cmd_goto_line(ed);
            break;

        case KEY_CTRL_O:
            cmd_paper_size(ed);
            break;

        case KEY_CTRL_P:
            cmd_insert_page_break(ed);
            break;

        case KEY_CTRL_W:
            cmd_toggle_wrap(ed);
            break;

        default:
            /* Printable character: insert at cursor */
            if (key >= 32 && key < 127) {
                ft_buffer_insert_char(&ed->buf, ed->cursor_row,
                                      ed->cursor_col, (char)key);
                ed->cursor_col++;
                ed->dirty = 1;
                maybe_wrap(ed);
            }
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                            */
/* ------------------------------------------------------------------ */

void ft_editor_init(FtEditor *ed)
{
    memset(ed, 0, sizeof(*ed));
    ft_buffer_init(&ed->buf);
    ed->cursor_row   = 0;
    ed->cursor_col   = 0;
    ed->scroll_row   = 0;
    ed->scroll_col   = 0;
    ed->dirty        = 0;
    ed->running      = 1;
    ed->clipboard    = NULL;
    ed->clipboard_len = 0;
    ed->paper_lines  = FT_PAPER_LETTER;
    ed->paper_cols   = FT_PAPER_COLS_LETTER;
    ed->wrap         = 1;
    ed->status_msg[0] = '\0';
    ed->filename[0]   = '\0';
    platform_get_size(&ed->screen_rows, &ed->screen_cols);
}

void ft_editor_free(FtEditor *ed)
{
    ft_buffer_free(&ed->buf);
    free(ed->clipboard);
    ed->clipboard = NULL;
}

void ft_editor_open(FtEditor *ed, const char *filename)
{
    strncpy(ed->filename, filename, FT_MAX_FILENAME - 1);
    ed->filename[FT_MAX_FILENAME - 1] = '\0';

    if (ft_buffer_load(&ed->buf, filename)) {
        ed->cursor_row = 0;
        ed->cursor_col = 0;
        ed->scroll_row = 0;
        ed->scroll_col = 0;
        ed->dirty      = 0;
    } else {
        /* File doesn't exist yet — start with an empty buffer */
        sprintf(ed->status_msg, "New file: %s", filename);
    }
}

int ft_editor_save(FtEditor *ed)
{
    if (!ed->filename[0])
        return 0;
    if (ft_buffer_save(&ed->buf, ed->filename)) {
        ed->dirty = 0;
        return 1;
    }
    return 0;
}

void ft_editor_run(FtEditor *ed)
{
    int key;

    while (ed->running) {
        draw_screen(ed);
        key = platform_read_key();
        process_key(ed, key);
    }
}
