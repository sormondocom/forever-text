/*
 * tests/test_buffer.c - Unit tests for the Forever Text buffer layer
 *
 * This file has no dependency on any platform layer and no terminal I/O.
 * It compiles and runs as a plain C89 program, which means it can be:
 *   - Run natively on the build host for a quick sanity check
 *   - Cross-compiled and run under QEMU user-mode on the CI runner
 *     to verify buffer correctness on every target architecture
 *
 * Exit code: 0 if all tests pass, 1 if any fail.
 */

#include <stdio.h>
#include <string.h>

#include "../src/buffer.h"

/* ------------------------------------------------------------------ */
/* Minimal test framework                                               */
/* ------------------------------------------------------------------ */

static int ft_passes   = 0;
static int ft_failures = 0;

#define ASSERT(cond, msg) \
    do { \
        if (cond) { \
            ft_passes++; \
        } else { \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
            ft_failures++; \
        } \
    } while (0)

#define ASSERT_INT_EQ(got, expected) \
    do { \
        int _g = (got); \
        int _e = (expected); \
        if (_g == _e) { \
            ft_passes++; \
        } else { \
            fprintf(stderr, "FAIL [%s:%d]: expected %d, got %d\n", \
                    __FILE__, __LINE__, _e, _g); \
            ft_failures++; \
        } \
    } while (0)

#define ASSERT_STR_EQ(got, expected) \
    do { \
        const char *_g = (got); \
        const char *_e = (expected); \
        if (_g && _e && strcmp(_g, _e) == 0) { \
            ft_passes++; \
        } else { \
            fprintf(stderr, "FAIL [%s:%d]: expected \"%s\", got \"%s\"\n", \
                    __FILE__, __LINE__, _e ? _e : "(null)", _g ? _g : "(null)"); \
            ft_failures++; \
        } \
    } while (0)

static void section(const char *name)
{
    printf("--- %s\n", name);
}

/* ------------------------------------------------------------------ */
/* Test helpers                                                          */
/* ------------------------------------------------------------------ */

/* Retrieve the text of a line as a C string for comparison */
static const char *line_text(FtBuffer *buf, int row)
{
    if (row < 0 || row >= buf->num_lines)
        return NULL;
    return buf->lines[row].data;
}

/* ------------------------------------------------------------------ */
/* Test cases                                                            */
/* ------------------------------------------------------------------ */

static void test_init_free(void)
{
    FtBuffer buf;
    section("init / free");

    ft_buffer_init(&buf);
    ASSERT_INT_EQ(buf.num_lines, 1);
    ASSERT_STR_EQ(line_text(&buf, 0), "");
    ft_buffer_free(&buf);

    /* After free the struct is zeroed out */
    ASSERT_INT_EQ(buf.num_lines, 0);
    ASSERT(buf.lines == NULL, "lines pointer is NULL after free");
}

static void test_insert_char(void)
{
    FtBuffer buf;
    section("insert_char");

    ft_buffer_init(&buf);

    /* Insert into an empty line */
    ASSERT_INT_EQ(ft_buffer_insert_char(&buf, 0, 0, 'H'), 1);
    ASSERT_INT_EQ(ft_buffer_insert_char(&buf, 0, 1, 'i'), 1);
    ASSERT_STR_EQ(line_text(&buf, 0), "Hi");
    ASSERT_INT_EQ(buf.lines[0].len, 2);

    /* Insert in the middle */
    ASSERT_INT_EQ(ft_buffer_insert_char(&buf, 0, 1, '!'), 1);
    ASSERT_STR_EQ(line_text(&buf, 0), "H!i");

    /* Invalid row */
    ASSERT_INT_EQ(ft_buffer_insert_char(&buf, 99, 0, 'X'), 0);

    /* Invalid col (beyond end) */
    ASSERT_INT_EQ(ft_buffer_insert_char(&buf, 0, 99, 'X'), 0);

    ft_buffer_free(&buf);
}

static void test_delete_char(void)
{
    FtBuffer buf;
    section("delete_char");

    ft_buffer_init(&buf);
    ft_buffer_insert_char(&buf, 0, 0, 'A');
    ft_buffer_insert_char(&buf, 0, 1, 'B');
    ft_buffer_insert_char(&buf, 0, 2, 'C');
    ASSERT_STR_EQ(line_text(&buf, 0), "ABC");

    /* Delete middle character */
    ASSERT_INT_EQ(ft_buffer_delete_char(&buf, 0, 1), 1);
    ASSERT_STR_EQ(line_text(&buf, 0), "AC");

    /* Delete first character */
    ASSERT_INT_EQ(ft_buffer_delete_char(&buf, 0, 0), 1);
    ASSERT_STR_EQ(line_text(&buf, 0), "C");

    /* Delete last character */
    ASSERT_INT_EQ(ft_buffer_delete_char(&buf, 0, 0), 1);
    ASSERT_STR_EQ(line_text(&buf, 0), "");
    ASSERT_INT_EQ(buf.lines[0].len, 0);

    /* Delete from empty line — invalid */
    ASSERT_INT_EQ(ft_buffer_delete_char(&buf, 0, 0), 0);

    ft_buffer_free(&buf);
}

static void test_split_line(void)
{
    FtBuffer buf;
    section("split_line");

    ft_buffer_init(&buf);
    /* Build "Hello World" on line 0 */
    {
        int i;
        const char *s = "Hello World";
        for (i = 0; s[i]; i++)
            ft_buffer_insert_char(&buf, 0, i, s[i]);
    }
    ASSERT_INT_EQ(buf.num_lines, 1);

    /* Split after "Hello" (col 5) */
    ASSERT_INT_EQ(ft_buffer_split_line(&buf, 0, 5), 1);
    ASSERT_INT_EQ(buf.num_lines, 2);
    ASSERT_STR_EQ(line_text(&buf, 0), "Hello");
    ASSERT_STR_EQ(line_text(&buf, 1), " World");

    /* Split at the beginning of a line */
    ASSERT_INT_EQ(ft_buffer_split_line(&buf, 0, 0), 1);
    ASSERT_INT_EQ(buf.num_lines, 3);
    ASSERT_STR_EQ(line_text(&buf, 0), "");
    ASSERT_STR_EQ(line_text(&buf, 1), "Hello");

    /* Split at the end of a line */
    ASSERT_INT_EQ(ft_buffer_split_line(&buf, 2, 6), 1);
    ASSERT_INT_EQ(buf.num_lines, 4);
    ASSERT_STR_EQ(line_text(&buf, 2), " World");
    ASSERT_STR_EQ(line_text(&buf, 3), "");

    ft_buffer_free(&buf);
}

static void test_join_lines(void)
{
    FtBuffer buf;
    section("join_lines");

    ft_buffer_init(&buf);
    ft_buffer_split_line(&buf, 0, 0); /* two empty lines */
    ft_buffer_split_line(&buf, 1, 0); /* three empty lines */
    ASSERT_INT_EQ(buf.num_lines, 3);

    /* Put text on lines 0 and 1 */
    {
        int i;
        const char *a = "foo";
        const char *b = "bar";
        for (i = 0; a[i]; i++) ft_buffer_insert_char(&buf, 0, i, a[i]);
        for (i = 0; b[i]; i++) ft_buffer_insert_char(&buf, 1, i, b[i]);
    }
    ASSERT_STR_EQ(line_text(&buf, 0), "foo");
    ASSERT_STR_EQ(line_text(&buf, 1), "bar");

    /* Join line 0 and line 1 */
    ASSERT_INT_EQ(ft_buffer_join_lines(&buf, 0), 1);
    ASSERT_INT_EQ(buf.num_lines, 2);
    ASSERT_STR_EQ(line_text(&buf, 0), "foobar");

    /* Join on invalid row */
    ASSERT_INT_EQ(ft_buffer_join_lines(&buf, 99), 0);

    /* Join the last two lines — second becomes empty */
    ASSERT_INT_EQ(ft_buffer_join_lines(&buf, 0), 1);
    ASSERT_INT_EQ(buf.num_lines, 1);
    ASSERT_STR_EQ(line_text(&buf, 0), "foobar");

    ft_buffer_free(&buf);
}

static void test_delete_line(void)
{
    FtBuffer buf;
    int i;
    section("delete_line");

    ft_buffer_init(&buf);
    /* Create three lines: "A", "B", "C" */
    for (i = 0; i < 2; i++)
        ft_buffer_split_line(&buf, i, 0);
    ft_buffer_insert_char(&buf, 0, 0, 'A');
    ft_buffer_insert_char(&buf, 1, 0, 'B');
    ft_buffer_insert_char(&buf, 2, 0, 'C');
    ASSERT_INT_EQ(buf.num_lines, 3);

    /* Delete the middle line */
    ASSERT_INT_EQ(ft_buffer_delete_line(&buf, 1), 1);
    ASSERT_INT_EQ(buf.num_lines, 2);
    ASSERT_STR_EQ(line_text(&buf, 0), "A");
    ASSERT_STR_EQ(line_text(&buf, 1), "C");

    /* Delete on invalid row */
    ASSERT_INT_EQ(ft_buffer_delete_line(&buf, 99), 0);

    /* Deleting the only remaining line clears it rather than removing it */
    ft_buffer_delete_line(&buf, 0);
    ft_buffer_delete_line(&buf, 0); /* now only one line */
    ASSERT_INT_EQ(buf.num_lines, 1);
    ASSERT_STR_EQ(line_text(&buf, 0), "");

    ft_buffer_free(&buf);
}

static void test_insert_line(void)
{
    FtBuffer buf;
    section("insert_line");

    ft_buffer_init(&buf);
    ft_buffer_insert_char(&buf, 0, 0, 'X');

    /* Insert a blank line before row 0 */
    ASSERT_INT_EQ(ft_buffer_insert_line(&buf, 0), 1);
    ASSERT_INT_EQ(buf.num_lines, 2);
    ASSERT_STR_EQ(line_text(&buf, 0), "");
    ASSERT_STR_EQ(line_text(&buf, 1), "X");

    /* Insert at the end */
    ASSERT_INT_EQ(ft_buffer_insert_line(&buf, 2), 1);
    ASSERT_INT_EQ(buf.num_lines, 3);
    ASSERT_STR_EQ(line_text(&buf, 2), "");

    ft_buffer_free(&buf);
}

static void test_get_line(void)
{
    FtBuffer buf;
    char dest[32];
    section("get_line");

    ft_buffer_init(&buf);
    ft_buffer_insert_char(&buf, 0, 0, 'H');
    ft_buffer_insert_char(&buf, 0, 1, 'i');

    ft_buffer_get_line(&buf, 0, dest, sizeof(dest));
    ASSERT_STR_EQ(dest, "Hi");

    /* Truncation: max=2 means only 1 char plus null */
    ft_buffer_get_line(&buf, 0, dest, 2);
    ASSERT_STR_EQ(dest, "H");

    /* Out-of-range row */
    ft_buffer_get_line(&buf, 99, dest, sizeof(dest));
    ASSERT_STR_EQ(dest, "");

    ft_buffer_free(&buf);
}

static void test_save_load(void)
{
    FtBuffer buf;
    FtBuffer buf2;
    const char *tmpfile = "ft_tmp.txt"; /* 8.3-safe: 6-char base */
    int i;
    section("save / load round-trip");

    ft_buffer_init(&buf);
    /* Build three lines */
    for (i = 0; i < 2; i++)
        ft_buffer_split_line(&buf, i, 0);
    {
        const char *lines[3];
        int r, c;
        lines[0] = "Hello, World!";
        lines[1] = "Second line.";
        lines[2] = "Third line.";
        for (r = 0; r < 3; r++)
            for (c = 0; lines[r][c]; c++)
                ft_buffer_insert_char(&buf, r, c, lines[r][c]);
    }

    ASSERT_INT_EQ(ft_buffer_save(&buf, tmpfile), 1);
    ft_buffer_free(&buf);

    ft_buffer_init(&buf2);
    ASSERT_INT_EQ(ft_buffer_load(&buf2, tmpfile), 1);
    ASSERT_INT_EQ(buf2.num_lines, 3);
    ASSERT_STR_EQ(line_text(&buf2, 0), "Hello, World!");
    ASSERT_STR_EQ(line_text(&buf2, 1), "Second line.");
    ASSERT_STR_EQ(line_text(&buf2, 2), "Third line.");
    ft_buffer_free(&buf2);

    /* Load a file that doesn't exist */
    ft_buffer_init(&buf);
    ASSERT_INT_EQ(ft_buffer_load(&buf, "this_file_does_not_exist.txt"), 0);
    ft_buffer_free(&buf);

    remove(tmpfile);
}

static void test_many_lines(void)
{
    FtBuffer buf;
    int i;
    section("many lines (growth stress)");

    ft_buffer_init(&buf);
    /* Insert 1000 lines */
    for (i = 0; i < 999; i++)
        ft_buffer_split_line(&buf, i, 0);
    ASSERT_INT_EQ(buf.num_lines, 1000);

    /* Delete them all back down to 1 */
    for (i = 999; i > 0; i--)
        ft_buffer_delete_line(&buf, i);
    ASSERT_INT_EQ(buf.num_lines, 1);

    ft_buffer_free(&buf);
}

static void test_long_line(void)
{
    FtBuffer buf;
    int i;
    section("long line (growth stress)");

    ft_buffer_init(&buf);
    /* Insert 4096 characters on one line */
    for (i = 0; i < 4096; i++)
        ft_buffer_insert_char(&buf, 0, i, (char)('A' + (i % 26)));
    ASSERT_INT_EQ(buf.lines[0].len, 4096);

    /* Delete them all */
    for (i = 4095; i >= 0; i--)
        ft_buffer_delete_char(&buf, 0, i);
    ASSERT_INT_EQ(buf.lines[0].len, 0);
    ASSERT_STR_EQ(line_text(&buf, 0), "");

    ft_buffer_free(&buf);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                           */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("Forever Text - buffer unit tests\n");
    printf("=================================\n");

    test_init_free();
    test_insert_char();
    test_delete_char();
    test_split_line();
    test_join_lines();
    test_delete_line();
    test_insert_line();
    test_get_line();
    test_save_load();
    test_many_lines();
    test_long_line();

    printf("=================================\n");
    printf("Results: %d passed, %d failed\n", ft_passes, ft_failures);

    return (ft_failures > 0) ? 1 : 0;
}
