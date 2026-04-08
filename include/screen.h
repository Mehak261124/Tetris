#ifndef SCREEN_H
#define SCREEN_H

/* =============================================================================
 * screen.h  —  Display / UI Module Public Interface
 * =============================================================================
 * All terminal output in the project flows through these functions.
 * No other module may call putchar / printf directly.
 * ============================================================================= */

/* ---- ANSI Color Code Constants ------------------------------------------- */
/* Foreground colors (pass to screen_set_color fg parameter) */
#define SCREEN_COLOR_DEFAULT  0
#define SCREEN_COLOR_BLACK   30
#define SCREEN_COLOR_RED     31
#define SCREEN_COLOR_GREEN   32
#define SCREEN_COLOR_YELLOW  33
#define SCREEN_COLOR_BLUE    34
#define SCREEN_COLOR_MAGENTA 35
#define SCREEN_COLOR_CYAN    36
#define SCREEN_COLOR_WHITE   37
/* Bright variants */
#define SCREEN_COLOR_BRIGHT_RED     91
#define SCREEN_COLOR_BRIGHT_GREEN   92
#define SCREEN_COLOR_BRIGHT_YELLOW  93
#define SCREEN_COLOR_BRIGHT_CYAN    96
#define SCREEN_COLOR_BRIGHT_WHITE   97

/* Background colors (pass to screen_set_color bg parameter, add 10) */
#define SCREEN_BG_BLACK   40
#define SCREEN_BG_RED     41
#define SCREEN_BG_GREEN   42
#define SCREEN_BG_YELLOW  43
#define SCREEN_BG_BLUE    44
#define SCREEN_BG_MAGENTA 45
#define SCREEN_BG_CYAN    46
#define SCREEN_BG_WHITE   47

/* ---- Function Declarations ------------------------------------------------ */

/* Clear the entire terminal screen and return cursor to (1,1) */
void screen_clear(void);

/* Move cursor to column x, row y (1-based) */
void screen_set_cursor(int x, int y);

/* Write a single character at current cursor position */
void screen_render_char(char c);

/* Write a null-terminated string at current cursor position */
void screen_render_string(const char *str);

/* Write an integer value without printf */
void screen_render_int(int value);

/* Set ANSI foreground (fg) and background (bg) colors.
   Pass SCREEN_COLOR_DEFAULT (0) to reset. */
void screen_set_color(int fg, int bg);

/* Reset all colors/attributes to terminal defaults */
void screen_reset_color(void);

/* Hide / restore the blinking terminal cursor */
void screen_hide_cursor(void);
void screen_show_cursor(void);

#endif /* SCREEN_H */
