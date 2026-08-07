/*
 * main.c - Entry point for Forever Text
 *
 * Usage:
 *   forever-text [filename]
 */

#include <stdio.h>
#include <stdlib.h>

#include "editor.h"
#include "platform/platform.h"

static FtEditor ed;

int main(int argc, char *argv[])
{
    platform_init();
    ft_editor_init(&ed);

    if (argc >= 2)
        ft_editor_open(&ed, argv[1]);

    ft_editor_run(&ed);

    ft_editor_free(&ed);
    platform_shutdown();

    return 0;
}
