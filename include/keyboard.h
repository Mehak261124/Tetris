#ifndef KEYBOARD_H
#define KEYBOARD_H

/* =============================================================================
 * keyboard.h  —  I/O Management Module (Input Side)
 * =============================================================================
 * Provides input abstraction for both terminal mode and WebSocket mode.
 * Terminal mode: raw non-blocking key reads from stdin.
 * WebSocket mode: non-blocking message reads from a socket client.
 * =============================================================================
 */

/* ---- Terminal mode (used by main.c terminal Tetris) ---------------------- */
void keyboard_init(void);                 /* enter raw/non-blocking mode */
void keyboard_restore(void);              /* restore normal terminal     */
char keyPressed(void);                    /* non-blocking single key read */
void readLine(char *buffer, int max_len); /* blocking line read         */

/* ---- WebSocket mode (used by ws_tetris.c backend) ------------------------ */
void keyboard_init_ws(int socket_fd);        /* set socket as input source  */
int keyboard_poll_ws(int timeout_ms);        /* poll socket for readability */
int keyboard_recv_ws(char *out, int out_sz); /* non-blocking WS msg read    */
void keyboard_close_ws(void);                /* close the socket fd         */

#endif /* KEYBOARD_H */
