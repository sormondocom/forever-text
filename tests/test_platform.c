/*
 * tests/test_platform.c - Platform layer compliance tests for Forever Text
 *
 * Usage:
 *   test-platform            interactive mode (needs a real terminal)
 *   test-platform headless   automatic checks only (CI / QEMU / pipe-safe)
 *
 * Automatic checks (both modes) verify:
 *   - platform_get_size() returns sane terminal dimensions
 *   - Every platform function accepts valid arguments without crashing
 *
 * Interactive checks (default mode only) verify:
 *   - Cursor positioning reaches all four screen corners
 *   - Reverse video attribute is visually distinct
 *   - clear_eol clears only to the right of the cursor
 *   - clear_screen blanks the full display
 *   - All KEY_* constants return the expected code when the real key is pressed
 *
 * Exit code: 0 = all checks passed, 1 = one or more failures.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/platform/platform.h"

/* ------------------------------------------------------------------ */
/* Result log (printed to stdout after platform_shutdown)               */
/* ------------------------------------------------------------------ */

#define MAX_LOG 96

static struct {
    char name[56];
    int  ok;
} g_log[MAX_LOG];

static int g_log_n = 0;
static int g_pass  = 0;
static int g_fail  = 0;
static int g_rows  = 0;
static int g_cols  = 0;

static void log_result(const char *name, int ok)
{
    int i;
    if (ok) g_pass++; else g_fail++;
    if (g_log_n < MAX_LOG) {
        for (i = 0; i < 55 && name[i]; i++)
            g_log[g_log_n].name[i] = name[i];
        g_log[g_log_n].name[i] = '\0';
        g_log[g_log_n].ok = ok;
        g_log_n++;
    }
}

static void print_results(void)
{
    int i;
    printf("Forever Text - platform compliance tests\n");
    printf("=========================================\n");
    for (i = 0; i < g_log_n; i++)
        printf("  %s: %s\n", g_log[i].ok ? "PASS" : "FAIL", g_log[i].name);
    printf("=========================================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
}

/* ------------------------------------------------------------------ */
/* Screen helpers (used while platform is initialised)                  */
/* ------------------------------------------------------------------ */

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void scr_puts(int row, int col, const char *s)
{
    platform_move(clamp_i(row, 0, g_rows - 1),
                  clamp_i(col, 0, g_cols - 1));
    platform_puts(s);
}

static void scr_centre(int row, const char *s)
{
    int len = (int)strlen(s);
    int col = (g_cols - len) / 2;
    scr_puts(row, col < 0 ? 0 : col, s);
}

/*
 * Display a pass/fail result on one screen row and log it.
 * Failed rows are shown in reverse video so they stand out.
 */
static void scr_record(int row, const char *name, int ok)
{
    int i;
    platform_move(clamp_i(row, 0, g_rows - 1), 0);
    platform_clear_eol();
    platform_move(clamp_i(row, 0, g_rows - 1), 0);
    if (!ok) platform_attr_reverse();
    platform_puts(ok ? "PASS" : "FAIL");
    platform_attr_normal();
    platform_puts(": ");
    for (i = 0; name[i] && i < g_cols - 8; i++)
        platform_putch(name[i]);
    log_result(name, ok);
}

/* Return 1 for yes, 0 for no; accepts Y/N/Enter/Escape */
static int ask_yn(void)
{
    int k;
    for (;;) {
        k = platform_read_key();
        if (k == 'y' || k == 'Y' || k == KEY_ENTER)  return 1;
        if (k == 'n' || k == 'N' || k == KEY_ESCAPE) return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Automatic crash-safety checks (headless-safe)                        */
/* ------------------------------------------------------------------ */

static void run_auto_checks(void)
{
    /* Screen dimensions */
    log_result("get_size: rows in range 1..999",
               g_rows > 0 && g_rows < 1000);
    log_result("get_size: cols in range 1..999",
               g_cols > 0 && g_cols < 1000);

    /* platform_move: four screen corners */
    platform_move(0, 0);
    platform_move(0, g_cols - 1);
    platform_move(g_rows - 1, 0);
    platform_move(g_rows - 1, g_cols - 1);
    log_result("platform_move: four corners without crash", 1);

    /* platform_putch: space, mid-ASCII, tilde */
    platform_move(g_rows - 1, 0);
    platform_putch(' ');
    platform_putch('A');
    platform_putch('~');
    log_result("platform_putch: space / printable ASCII / tilde", 1);

    /* platform_puts: empty, single char, sentence */
    platform_move(g_rows - 1, 0);
    platform_puts("");
    platform_puts("x");
    platform_puts("hello, world");
    log_result("platform_puts: empty and non-empty strings", 1);

    /* platform_putn: n=0, n=3, n=6 */
    platform_move(g_rows - 1, 0);
    platform_putn("abcdef", 0);
    platform_putn("abcdef", 3);
    platform_putn("abcdef", 6);
    log_result("platform_putn: n=0, n=3, n=6 without crash", 1);

    /* platform_attr_reverse / platform_attr_normal: toggle */
    platform_move(g_rows - 1, 0);
    platform_attr_reverse();
    platform_putch('R');
    platform_attr_normal();
    platform_putch('N');
    log_result("platform_attr_reverse/normal: toggle without crash", 1);

    /* platform_clear_eol */
    platform_move(g_rows - 1, 0);
    platform_clear_eol();
    log_result("platform_clear_eol: without crash", 1);

    /* platform_flush */
    platform_flush();
    log_result("platform_flush: without crash", 1);

    /* platform_clear_screen — called last so it doesn't erase prior output */
    platform_clear_screen();
    log_result("platform_clear_screen: without crash", 1);

    platform_flush();
}

/* ------------------------------------------------------------------ */
/* Interactive visual tests                                             */
/* ------------------------------------------------------------------ */

static void run_visual_tests(void)
{
    int mid_r = g_rows / 2;
    int ok;

    /* --- Cursor positioning --- */
    platform_clear_screen();
    scr_puts(0,          0,          "TL");
    scr_puts(0,          g_cols - 2, "TR");
    scr_puts(g_rows - 1, 0,          "BL");
    scr_puts(g_rows - 1, g_cols - 2, "BR");
    scr_centre(mid_r - 1, "-- CURSOR POSITION TEST --");
    scr_centre(mid_r,     "Text in all 4 corners? [Y/N]");
    platform_flush();
    ok = ask_yn();
    scr_record(mid_r + 2, "cursor_move: text placed at all 4 screen corners", ok);
    platform_flush();

    /* --- Reverse video --- */
    platform_clear_screen();
    {
        int col = (g_cols - 22) / 2;
        platform_move(mid_r - 1, col < 0 ? 0 : col);
        platform_attr_reverse();
        platform_puts("  REVERSE VIDEO TEXT  ");
        platform_attr_normal();
    }
    scr_centre(mid_r,     "-- REVERSE VIDEO TEST --");
    scr_centre(mid_r + 1, "Is the text above shown reversed? [Y/N]");
    platform_flush();
    ok = ask_yn();
    scr_record(mid_r + 3, "attr_reverse: text visually distinct from normal", ok);
    platform_flush();

    /* --- Clear to end of line --- */
    platform_clear_screen();
    scr_puts(mid_r - 2, 0, "XXXXXXXXXXXXXXXXXXXXXXXXXX");
    platform_move(mid_r - 2, 5);
    platform_clear_eol();
    scr_centre(mid_r,     "-- CLEAR-EOL TEST --");
    scr_centre(mid_r + 1, "5 X's, then blank to the right? [Y/N]");
    platform_flush();
    ok = ask_yn();
    scr_record(mid_r + 3, "clear_eol: clears from cursor column to line end", ok);
    platform_flush();

    /* --- Clear screen --- */
    platform_clear_screen();
    /* Fill with noise, then clear */
    scr_puts(mid_r - 2, 0, "ZZZZZZZZZZZZZZZZZZZZZZZZZZ");
    platform_flush();
    platform_clear_screen();
    scr_centre(mid_r,     "-- CLEAR SCREEN TEST --");
    scr_centre(mid_r + 1, "Screen blank above this line? [Y/N]");
    platform_flush();
    ok = ask_yn();
    scr_record(mid_r + 3, "clear_screen: entire screen cleared", ok);
    platform_flush();

    scr_centre(mid_r + 5, "Visual tests done. Press any key for key tests...");
    platform_flush();
    platform_read_key();
}

/* ------------------------------------------------------------------ */
/* Interactive key-code tests                                           */
/* ------------------------------------------------------------------ */

/*
 * Prompt for one key, verify its code, show pass/fail on screen, log it.
 * result_row: which display row to write the pass/fail to.
 */
static void run_one_key_test(int result_row, const char *label, int expected)
{
    int got, i;
    char msg[64];

    /* Prompt at a fixed row near the bottom */
    platform_move(g_rows - 3, 0);
    platform_clear_eol();
    platform_move(g_rows - 3, 0);
    platform_puts("Press: ");
    platform_puts(label);
    platform_flush();

    got = platform_read_key();

    /* Build log label "read_key: LABEL" */
    {
        const char *pfx = "read_key: ";
        int p, l;
        for (p = 0; pfx[p] && p < 12; p++) msg[p] = pfx[p];
        for (l = 0; label[l] && p + l < 62; l++) msg[p + l] = label[l];
        msg[p + l] = '\0';
    }

    /* Show result on the scrolling results area */
    if (result_row < g_rows - 4) {
        platform_move(result_row, 0);
        platform_clear_eol();
        platform_move(result_row, 0);
        if (got == expected) {
            platform_puts("PASS: ");
        } else {
            platform_attr_reverse();
            platform_puts("FAIL: ");
            platform_attr_normal();
            /* Print got/expected at the right margin */
            {
                char tmp[24];
                int tlen;
                sprintf(tmp, "[got=%d exp=%d]", got, expected);
                tlen = (int)strlen(tmp);
                platform_move(result_row, g_cols - tlen - 1);
                platform_puts(tmp);
                platform_move(result_row, 6);
            }
        }
        for (i = 0; label[i] && i < g_cols - 24; i++)
            platform_putch(label[i]);
        platform_flush();
    }

    log_result(msg, got == expected);
}

static void run_key_tests(void)
{
    /* Keys to test: all KEY_* constants that the editor actually uses */
    struct { const char *label; int expected; } keys[] = {
        { "ENTER",        KEY_ENTER       },
        { "BACKSPACE",    KEY_BACKSPACE   },
        { "ESCAPE",       KEY_ESCAPE      },
        { "TAB",          KEY_TAB         },
        { "DELETE",       KEY_DELETE      },
        { "UP arrow",     KEY_UP          },
        { "DOWN arrow",   KEY_DOWN        },
        { "LEFT arrow",   KEY_LEFT        },
        { "RIGHT arrow",  KEY_RIGHT       },
        { "PAGE UP",      KEY_PAGE_UP     },
        { "PAGE DOWN",    KEY_PAGE_DOWN   },
        { "HOME",         KEY_HOME        },
        { "END",          KEY_END         },
        { "Ctrl+S (Save)",  KEY_CTRL_S    },
        { "Ctrl+L (Load)",  KEY_CTRL_L    },
        { "Ctrl+Q (Quit)",  KEY_CTRL_Q    },
        { "Ctrl+K (Cut)",   KEY_CTRL_K    },
        { "Ctrl+U (Paste)", KEY_CTRL_U    },
        { "Ctrl+F (Find)",  KEY_CTRL_F    },
        { "Ctrl+G (Goto)",  KEY_CTRL_G    },
        { "Ctrl+O (Paper)", KEY_CTRL_O    },
        { "Ctrl+P (PgBrk)", KEY_CTRL_P    },
        { "Ctrl+W (Wrap)",  KEY_CTRL_W    }
    };
    int n = (int)(sizeof(keys) / sizeof(keys[0]));
    int i;
    int result_start = 2; /* first screen row for results */

    platform_clear_screen();
    scr_puts(0, 0, "-- KEY INPUT TESTS --");
    scr_puts(1, 0, "Results:");
    platform_flush();

    for (i = 0; i < n; i++)
        run_one_key_test(result_start + i, keys[i].label, keys[i].expected);

    platform_move(g_rows - 3, 0); platform_clear_eol();
    platform_move(g_rows - 2, 0); platform_clear_eol();
    scr_centre(g_rows - 2, "Key tests done.  Press any key to see summary...");
    platform_flush();
    platform_read_key();
}

/* ------------------------------------------------------------------ */
/* On-screen summary (interactive mode, shown before shutdown)          */
/* ------------------------------------------------------------------ */

static void show_summary(void)
{
    int mid_r = g_rows / 2;
    char buf[48];

    platform_clear_screen();
    scr_centre(mid_r - 3, "=========================================");
    scr_centre(mid_r - 2, "  FOREVER TEXT  PLATFORM TEST RESULTS  ");
    scr_centre(mid_r - 1, "=========================================");

    sprintf(buf, "Passed: %d", g_pass);
    scr_centre(mid_r + 1, buf);

    sprintf(buf, "Failed: %d", g_fail);
    if (g_fail > 0) platform_attr_reverse();
    scr_centre(mid_r + 2, buf);
    if (g_fail > 0) platform_attr_normal();

    if (g_fail == 0)
        scr_centre(mid_r + 4, "[ ALL TESTS PASSED ]");
    else
        scr_centre(mid_r + 4, "[ FAILURES DETECTED ]");

    scr_centre(mid_r + 6, "Press any key to exit...");
    platform_flush();
    platform_read_key();
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    int headless = (argc > 1 && strcmp(argv[1], "headless") == 0);

    platform_init();
    platform_get_size(&g_rows, &g_cols);

    run_auto_checks();

    if (!headless) {
        platform_clear_screen();
        scr_puts(0, 0,
            "Automatic checks complete.  Press any key for visual tests...");
        platform_flush();
        platform_read_key();

        run_visual_tests();
        run_key_tests();
        show_summary();
    }

    platform_shutdown();

    print_results();
    return g_fail > 0 ? 1 : 0;
}
