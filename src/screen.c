/* =============================================================================
 * screen.c  —  User Interface / Display Output Module
 * =============================================================================
 * OS MODULE: I/O Management (Output side) + User Interface
 *   Abstracts every pixel/character drawn to the terminal — analogous to a
 *   framebuffer driver in a real OS.  All rendering in the project routes
 *   through this file; no other module may write to stdout directly.
 *
 * TERMINAL CONTROL via ANSI Escape Sequences:
 *   \033[2J     — erase the whole display
 *   \033[H      — move cursor to top-left (home position)
 *   \033[Y;XH   — move cursor to row Y, column X (1-based)
 *   \033[0m     — reset all attributes (color, bold, etc.)
 *   \033[1m     — bold
 *   \033[3Xm    — set foreground color (30-37 = standard, 90-97 = bright)
 *   \033[4Xm    — set background color
 *
 * RULES COMPLIANCE:
 *   - <stdio.h> : allowed for terminal I/O (putchar, fflush).
 *   - printf / scanf : NOT used. All output is built from putchar() calls
 *     routed through our own screen_render_* helpers.
 * ============================================================================= */

#include "../include/screen.h"
#include <stdio.h>    /* putchar(), fflush() — terminal I/O, allowed by spec */

/* ---------------------------------------------------------------------------
 * Internal helper: write_escape_num(n)
 *   Writes an unsigned integer n to stdout without using printf.
 *   Used to build ANSI escape sequences (row/col numbers, color codes).
 *
 *   Algorithm: extract digits in reverse order into a tiny local buffer,
 *   then write them out front-to-back.
 * --------------------------------------------------------------------------- */
static void write_escape_num(int n) {
    /* Handle 0 explicitly */
    if (n == 0) { putchar('0'); return; }

    char buf[12];           /* big enough for any 32-bit decimal              */
    int  len = 0;

    /* Build digits in reverse */
    while (n > 0) {
        buf[len++] = (char)('0' + (n % 10));
        n /= 10;
    }

    /* Write them in correct order */
    int i = len - 1;
    while (i >= 0) {
        putchar(buf[i]);
        i--;
    }
}

/* ---------------------------------------------------------------------------
 * screen_clear()
 *   Erases all content on the terminal and moves the cursor to (1,1).
 *   Call at the start of every frame to avoid ghosting from the previous draw.
 * --------------------------------------------------------------------------- */
void screen_clear(void) {
    /* \033[2J  = erase entire display
       \033[H   = cursor to home (row 1, col 1) */
    putchar('\033'); putchar('['); putchar('2'); putchar('J');
    putchar('\033'); putchar('['); putchar('H');
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * screen_set_cursor(x, y)
 *   Moves the terminal cursor to column x, row y (both 1-based).
 *
 *   ANSI sequence: ESC [ row ; col H
 *   Note the order: row comes first in the escape sequence, but our API uses
 *   (x=col, y=row) so callers can think in Cartesian coordinates.
 *
 *   Edge case: x or y of 0 is clamped to 1 (terminals are 1-based).
 * --------------------------------------------------------------------------- */
void screen_set_cursor(int x, int y) {
    if (x < 1) x = 1;
    if (y < 1) y = 1;

    putchar('\033'); putchar('[');
    write_escape_num(y);          /* row first */
    putchar(';');
    write_escape_num(x);          /* then column */
    putchar('H');
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * screen_render_char(c)
 *   Writes a single character at the current cursor position.
 *   The terminal then advances the cursor one column to the right.
 * --------------------------------------------------------------------------- */
void screen_render_char(char c) {
    putchar(c);
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * screen_render_string(str)
 *   Writes every character in a null-terminated string to stdout.
 *   Safe on NULL input (no-op).
 * --------------------------------------------------------------------------- */
void screen_render_string(const char *str) {
    if (!str) return;
    int i = 0;
    while (str[i] != '\0') {
        putchar(str[i]);
        i++;
    }
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * screen_set_color(fg, bg)
 *   Sets ANSI foreground and background colors.
 *
 *   fg / bg use the SCREEN_COLOR_* constants defined in screen.h:
 *     0  = default (reset)
 *     30-37 = standard colors (BLACK … WHITE)
 *     90-97 = bright colors
 *
 *   Pass fg=0 to reset both colors to terminal defaults.
 *
 *   Example: screen_set_color(SCREEN_COLOR_CYAN, SCREEN_COLOR_DEFAULT)
 * --------------------------------------------------------------------------- */
void screen_set_color(int fg, int bg) {
    putchar('\033'); putchar('[');

    if (fg == 0 && bg == 0) {
        /* Full reset */
        putchar('0');
    } else {
        if (fg > 0) {
            write_escape_num(fg);
        }
        if (bg > 0) {
            putchar(';');
            write_escape_num(bg);
        }
    }

    putchar('m');
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * screen_reset_color()
 *   Convenience wrapper: reset all ANSI color/attribute codes to defaults.
 *   Always call this after drawing colored content to avoid color bleed into
 *   subsequent text.
 * --------------------------------------------------------------------------- */
void screen_reset_color(void) {
    putchar('\033'); putchar('['); putchar('0'); putchar('m');
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * screen_render_int(value)
 *   Renders an integer directly to the terminal without sprintf / printf.
 *   Uses write_escape_num internally; handles negative values.
 * --------------------------------------------------------------------------- */
void screen_render_int(int value) {
    if (value < 0) {
        putchar('-');
        value = -value;
    }
    write_escape_num(value);
    fflush(stdout);
}

/* ---------------------------------------------------------------------------
 * screen_hide_cursor() / screen_show_cursor()
 *   Hides or restores the blinking terminal cursor during gameplay so it
 *   does not visually interfere with the Tetris board rendering.
 * --------------------------------------------------------------------------- */
void screen_hide_cursor(void) {
    putchar('\033'); putchar('['); putchar('?');
    putchar('2'); putchar('5'); putchar('l');
    fflush(stdout);
}

void screen_show_cursor(void) {
    putchar('\033'); putchar('['); putchar('?');
    putchar('2'); putchar('5'); putchar('h');
    fflush(stdout);
}
