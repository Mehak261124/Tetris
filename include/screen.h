#ifndef SCREEN_H
#define SCREEN_H

/* ---- ANSI Color Constants ------------------------------------------------ */
#define SCREEN_COLOR_DEFAULT        0
#define SCREEN_COLOR_BLACK         30
#define SCREEN_COLOR_RED           31
#define SCREEN_COLOR_GREEN         32
#define SCREEN_COLOR_YELLOW        33
#define SCREEN_COLOR_BLUE          34
#define SCREEN_COLOR_MAGENTA       35
#define SCREEN_COLOR_CYAN          36
#define SCREEN_COLOR_WHITE         37
#define SCREEN_COLOR_BRIGHT_RED    91
#define SCREEN_COLOR_BRIGHT_GREEN  92
#define SCREEN_COLOR_BRIGHT_YELLOW 93
#define SCREEN_COLOR_BRIGHT_CYAN   96
#define SCREEN_COLOR_BRIGHT_WHITE  97
#define SCREEN_BG_BLACK   40
#define SCREEN_BG_RED     41
#define SCREEN_BG_GREEN   42
#define SCREEN_BG_YELLOW  43
#define SCREEN_BG_BLUE    44
#define SCREEN_BG_MAGENTA 45
#define SCREEN_BG_CYAN    46
#define SCREEN_BG_WHITE   47

/* ---- Alternate screen buffer --------------------------------------------- */
void screen_enter_alt(void);    /* \033[?1049h — switch to blank alt screen  */
void screen_exit_alt(void);     /* \033[?1049l — restore original screen      */

/* ---- Mouse input isolation ----------------------------------------------- */
void screen_disable_mouse(void); /* stop scroll/click escape sequences        */
void screen_enable_mouse(void);  /* restore terminal mouse state on exit      */

/* ---- Terminal output ----------------------------------------------------- */
void screen_clear(void);
void screen_set_cursor(int x, int y);
void screen_render_char(char c);
void screen_render_string(const char *str);
void screen_render_int(int value);
void screen_set_color(int fg, int bg);
void screen_reset_color(void);
void screen_hide_cursor(void);
void screen_show_cursor(void);
void screen_get_size(int *cols, int *rows);
void screen_panic(const char *msg);   /* exits alt screen + restores terminal */

#define T_PANIC(m) screen_panic(m)

/* ---- WebSocket output ---------------------------------------------------- */
int  screen_server_start(int port);
int  screen_server_listen(int port);
int  screen_server_accept(int srv_fd);
void screen_server_close(int srv_fd);
void screen_init_ws(int socket_fd);
int  screen_ws_handshake(void);
int  screen_send_ws(const char *json);

#endif /* SCREEN_H */