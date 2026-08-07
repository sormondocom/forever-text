/*
 * buffer.c - Text buffer implementation for Forever Text
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"

#define INIT_LINE_CAP   64
#define INIT_LINES_CAP  64

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static int line_ensure_cap(FtLine *ln, int needed)
{
    char *p;
    int   cap;

    if (needed <= ln->cap)
        return 1;

    cap = ln->cap ? ln->cap : INIT_LINE_CAP;
    while (cap < needed)
        cap *= 2;

    p = (char *)realloc(ln->data, (size_t)cap);
    if (!p)
        return 0;

    ln->data = p;
    ln->cap  = cap;
    return 1;
}

static int buf_ensure_cap(FtBuffer *buf, int needed)
{
    FtLine *p;
    int     cap;

    if (needed <= buf->cap)
        return 1;

    cap = buf->cap ? buf->cap : INIT_LINES_CAP;
    while (cap < needed)
        cap *= 2;

    p = (FtLine *)realloc(buf->lines, (size_t)cap * sizeof(FtLine));
    if (!p)
        return 0;

    buf->lines = p;
    buf->cap   = cap;
    return 1;
}

static void line_init(FtLine *ln)
{
    ln->data    = NULL;
    ln->len     = 0;
    ln->cap     = 0;
}

static void line_free(FtLine *ln)
{
    free(ln->data);
    ln->data = NULL;
    ln->len  = 0;
    ln->cap  = 0;
}

/* Set a line's content from a char* of length len (does not include '\0') */
static int line_set(FtLine *ln, const char *src, int len)
{
    if (!line_ensure_cap(ln, len + 1))
        return 0;
    if (len > 0)
        memcpy(ln->data, src, (size_t)len);
    ln->data[len] = '\0';
    ln->len = len;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void ft_buffer_init(FtBuffer *buf)
{
    buf->lines     = NULL;
    buf->num_lines = 0;
    buf->cap       = 0;

    /* Every buffer starts with one empty line */
    if (buf_ensure_cap(buf, 1)) {
        line_init(&buf->lines[0]);
        line_set(&buf->lines[0], "", 0);
        buf->num_lines = 1;
    }
}

void ft_buffer_free(FtBuffer *buf)
{
    int i;
    for (i = 0; i < buf->num_lines; i++)
        line_free(&buf->lines[i]);
    free(buf->lines);
    buf->lines     = NULL;
    buf->num_lines = 0;
    buf->cap       = 0;
}

int ft_buffer_insert_char(FtBuffer *buf, int row, int col, char c)
{
    FtLine *ln;

    if (row < 0 || row >= buf->num_lines)
        return 0;

    ln = &buf->lines[row];

    if (col < 0 || col > ln->len)
        return 0;

    if (!line_ensure_cap(ln, ln->len + 2))
        return 0;

    /* Shift everything from col rightward by one */
    memmove(ln->data + col + 1, ln->data + col,
            (size_t)(ln->len - col + 1)); /* +1 to move the '\0' */
    ln->data[col] = c;
    ln->len++;
    return 1;
}

int ft_buffer_delete_char(FtBuffer *buf, int row, int col)
{
    FtLine *ln;

    if (row < 0 || row >= buf->num_lines)
        return 0;

    ln = &buf->lines[row];

    if (col < 0 || col >= ln->len)
        return 0;

    /* Shift everything from col+1 leftward by one (includes '\0') */
    memmove(ln->data + col, ln->data + col + 1,
            (size_t)(ln->len - col));
    ln->len--;
    return 1;
}

int ft_buffer_split_line(FtBuffer *buf, int row, int col)
{
    FtLine *old_ln;
    FtLine  new_ln;
    int     tail_len;

    if (row < 0 || row >= buf->num_lines)
        return 0;

    old_ln   = &buf->lines[row];
    tail_len = old_ln->len - col;

    if (tail_len < 0)
        return 0;

    /* Build the new line from the text after col */
    line_init(&new_ln);
    if (!line_set(&new_ln, old_ln->data + col, tail_len))
        return 0;

    /* Truncate the original line at col */
    old_ln->data[col] = '\0';
    old_ln->len       = col;

    /* Make room in the lines array */
    if (!buf_ensure_cap(buf, buf->num_lines + 1)) {
        line_free(&new_ln);
        return 0;
    }

    /* Shift lines after row down by one */
    memmove(buf->lines + row + 2, buf->lines + row + 1,
            (size_t)(buf->num_lines - row - 1) * sizeof(FtLine));

    buf->lines[row + 1] = new_ln;
    buf->num_lines++;
    return 1;
}

int ft_buffer_join_lines(FtBuffer *buf, int row)
{
    FtLine *a;
    FtLine *b;
    int     new_len;

    if (row < 0 || row + 1 >= buf->num_lines)
        return 0;

    a = &buf->lines[row];
    b = &buf->lines[row + 1];

    new_len = a->len + b->len;
    if (!line_ensure_cap(a, new_len + 1))
        return 0;

    memcpy(a->data + a->len, b->data, (size_t)b->len + 1); /* copy '\0' too */
    a->len = new_len;

    /* Free the consumed line and close the gap */
    line_free(b);
    memmove(buf->lines + row + 1, buf->lines + row + 2,
            (size_t)(buf->num_lines - row - 2) * sizeof(FtLine));
    buf->num_lines--;
    return 1;
}

int ft_buffer_delete_line(FtBuffer *buf, int row)
{
    if (row < 0 || row >= buf->num_lines)
        return 0;

    /* Never delete the last remaining line; just clear it instead */
    if (buf->num_lines == 1) {
        buf->lines[0].data[0] = '\0';
        buf->lines[0].len     = 0;
        return 1;
    }

    line_free(&buf->lines[row]);
    memmove(buf->lines + row, buf->lines + row + 1,
            (size_t)(buf->num_lines - row - 1) * sizeof(FtLine));
    buf->num_lines--;
    return 1;
}

int ft_buffer_insert_line(FtBuffer *buf, int row)
{
    FtLine blank;

    if (row < 0 || row > buf->num_lines)
        return 0;

    if (!buf_ensure_cap(buf, buf->num_lines + 1))
        return 0;

    memmove(buf->lines + row + 1, buf->lines + row,
            (size_t)(buf->num_lines - row) * sizeof(FtLine));

    line_init(&blank);
    if (!line_set(&blank, "", 0)) {
        /* Undo the shift */
        memmove(buf->lines + row, buf->lines + row + 1,
                (size_t)(buf->num_lines - row) * sizeof(FtLine));
        return 0;
    }

    buf->lines[row] = blank;
    buf->num_lines++;
    return 1;
}

void ft_buffer_get_line(FtBuffer *buf, int row, char *dest, int max)
{
    FtLine *ln;
    int     n;

    if (row < 0 || row >= buf->num_lines || max <= 0) {
        if (max > 0) dest[0] = '\0';
        return;
    }

    ln = &buf->lines[row];
    n  = ln->len < (max - 1) ? ln->len : (max - 1);
    memcpy(dest, ln->data, (size_t)n);
    dest[n] = '\0';
}

int ft_buffer_load(FtBuffer *buf, const char *filename)
{
    FILE        *fp;
    static char  line_buf[4096];
    int    i;
    int    len;
    char  *nl;
    FtLine new_ln;

    fp = fopen(filename, "r");
    if (!fp)
        return 0;

    /* Drop existing content */
    for (i = 0; i < buf->num_lines; i++)
        line_free(&buf->lines[i]);
    buf->num_lines = 0;

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        nl = strchr(line_buf, '\n');
        if (nl) *nl = '\0';

        /* Also strip CR for files with CRLF line endings */
        len = (int)strlen(line_buf);
        if (len > 0 && line_buf[len - 1] == '\r') {
            line_buf[--len] = '\0';
        }

        if (!buf_ensure_cap(buf, buf->num_lines + 1)) {
            fclose(fp);
            return 0;
        }

        line_init(&new_ln);
        if (!line_set(&new_ln, line_buf, len)) {
            fclose(fp);
            return 0;
        }

        buf->lines[buf->num_lines] = new_ln;
        buf->num_lines++;
    }

    fclose(fp);

    /* Always ensure at least one line */
    if (buf->num_lines == 0) {
        if (buf_ensure_cap(buf, 1)) {
            line_init(&buf->lines[0]);
            line_set(&buf->lines[0], "", 0);
            buf->num_lines = 1;
        }
    }

    return 1;
}

int ft_buffer_save(FtBuffer *buf, const char *filename)
{
    FILE *fp;
    int   i;

    fp = fopen(filename, "w");
    if (!fp)
        return 0;

    for (i = 0; i < buf->num_lines; i++) {
        fputs(buf->lines[i].data, fp);
        fputc('\n', fp);
    }

    fclose(fp);
    return 1;
}
