#ifndef SCREEN_H
#define SCREEN_H

/* =============================================================================
 * screen.h  —  Display / UI Module Public Interface
 * =============================================================================
 * All output in the project flows through these functions.
 * Terminal mode: ANSI escape sequences to the local terminal.
 * WebSocket mode: WebSocket text frames to a browser client.
 * =============================================================================
 */

/* ---- ANSI Color Code Constants ------------------------------------------- */
/* Foreground colors (pass to screen_set_color fg parameter) */
#define SCREEN_COLOR_DEFAULT 0
#define SCREEN_COLOR_BLACK 30
#define SCREEN_COLOR_RED 31
#define SCREEN_COLOR_GREEN 32
#define SCREEN_COLOR_YELLOW 33
#define SCREEN_COLOR_BLUE 34
#define SCREEN_COLOR_MAGENTA 35
#define SCREEN_COLOR_CYAN 36
#define SCREEN_COLOR_WHITE 37
/* Bright variants */
#define SCREEN_COLOR_BRIGHT_RED 91
#define SCREEN_COLOR_BRIGHT_GREEN 92
#define SCREEN_COLOR_BRIGHT_YELLOW 93
#define SCREEN_COLOR_BRIGHT_CYAN 96
#define SCREEN_COLOR_BRIGHT_WHITE 97

/* Background colors (pass to screen_set_color bg parameter, add 10) */
#define SCREEN_BG_BLACK 40
#define SCREEN_BG_RED 41
#define SCREEN_BG_GREEN 42
#define SCREEN_BG_YELLOW 43
#define SCREEN_BG_BLUE 44
#define SCREEN_BG_MAGENTA 45
#define SCREEN_BG_CYAN 46
#define SCREEN_BG_WHITE 47

/* ---- Terminal mode functions ----------------------------------------------
 */

void screen_clear(void);
void screen_set_cursor(int x, int y);
void screen_render_char(char c);
void screen_render_string(const char *str);
void screen_render_int(int value);
void screen_set_color(int fg, int bg);
void screen_reset_color(void);
void screen_hide_cursor(void);
void screen_show_cursor(void);
void screen_get_size(int *cols, int *rows); /* query terminal dimensions */

/* ---- WebSocket mode functions ---------------------------------------------
 */

int screen_server_start(int port);    /* start WS server, return fd  */
int screen_server_listen(int port);   /* bind+listen, return server_fd */
int screen_server_accept(int srv_fd); /* accept one client, return fd */
void screen_server_close(int srv_fd); /* close the server socket      */
void screen_init_ws(int socket_fd);   /* set socket as output target */
int screen_ws_handshake(void);        /* perform WS upgrade          */
int screen_send_ws(const char *json); /* send a WS text frame        */

#endif /* SCREEN_H */
