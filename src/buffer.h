/*
 * buffer.h - Text buffer interface for Forever Text
 *
 * The buffer is a dynamic array of lines.  Each line owns its character
 * data.  All row/col coordinates are 0-based throughout this API.
 */

#ifndef FT_BUFFER_H
#define FT_BUFFER_H

/* One line of text */
typedef struct {
    char *data;  /* heap-allocated, always null-terminated */
    int   len;   /* number of characters (not counting the null) */
    int   cap;   /* allocated capacity including the null byte */
} FtLine;

/* The complete text buffer */
typedef struct {
    FtLine *lines;     /* heap-allocated array of FtLine */
    int     num_lines; /* number of lines in use */
    int     cap;       /* allocated capacity of the lines array */
} FtBuffer;

void ft_buffer_init(FtBuffer *buf);
void ft_buffer_free(FtBuffer *buf);

/* Insert character c before position col on line row. */
int ft_buffer_insert_char(FtBuffer *buf, int row, int col, char c);

/* Delete the character at (row, col). */
int ft_buffer_delete_char(FtBuffer *buf, int row, int col);

/* Split line[row] at col: text from col onward becomes line[row+1]. */
int ft_buffer_split_line(FtBuffer *buf, int row, int col);

/* Join line[row+1] onto the end of line[row] and remove line[row+1]. */
int ft_buffer_join_lines(FtBuffer *buf, int row);

/* Delete the entire line at row. */
int ft_buffer_delete_line(FtBuffer *buf, int row);

/* Insert a new blank line before row. */
int ft_buffer_insert_line(FtBuffer *buf, int row);

/* Copy the text of line[row] into dest (up to max-1 chars). */
void ft_buffer_get_line(FtBuffer *buf, int row, char *dest, int max);

/* Load file into buf, replacing any existing content. Returns 1 on success. */
int ft_buffer_load(FtBuffer *buf, const char *filename);

/* Write buf to filename. Returns 1 on success. */
int ft_buffer_save(FtBuffer *buf, const char *filename);

#endif /* FT_BUFFER_H */
