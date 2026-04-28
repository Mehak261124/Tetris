/* =============================================================================
 * keyboard.c  —  Input/Output Management Module
 * =============================================================================
 * OS MODULE: I/O Management
 *   Manages how this "computer" (our Tetris OS) talks to the world.
 *   Supports TWO input modes:
 *     1. Terminal mode — raw, non-blocking key reads from stdin.
 *     2. WebSocket mode — non-blocking message reads from a socket client.
 *
 * VS CODE TERMINAL COMPATIBILITY:
 *   VS Code's integrated terminal uses a pty layer that differs from a real
 *   xterm.  Two specific fixes are applied:
 *     a) stty uses "cbreak" instead of "raw" so VS Code's Ctrl+C signal path
 *        still functions correctly.
 *     b) keyPressed() explicitly checks for ASCII ETX (0x03 = Ctrl+C) as a
 *        software fallback so the game can quit even if SIGINT is not delivered.
 *
 * RULES COMPLIANCE:
 *   - <stdio.h>  : allowed (terminal I/O only — getchar / EOF)
 *   - <stdlib.h>  : allowed (system() for stty terminal control)
 *   - Networking headers are used ONLY for WebSocket mode.
 * =============================================================================
 */

#include "../include/keyboard.h"
#include <stdio.h>
#include <stdlib.h>

/* ---- Hardware Abstraction headers for WebSocket mode --------------------- */
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* ========================== TERMINAL MODE ================================= */

/*
 * keyboard_init()
 *   Switches the terminal into a non-blocking, no-echo mode.
 *
 *   We use "cbreak" instead of "raw" for two reasons:
 *     1. cbreak still delivers SIGINT on Ctrl+C at the OS level, which
 *        the SIGINT handler in main.c catches and sets g_quit_signal.
 *     2. VS Code's integrated terminal (and most modern terminal emulators)
 *        handle cbreak more reliably than full raw mode.
 *
 *   "min 0 time 0" makes reads non-blocking: getchar() returns EOF
 *   immediately if no key is pending, which is what keyPressed() needs.
 *
 *   "-echo" suppresses character echo so typed keys don't appear on screen.
 *   "-icanon" disables line-buffering (each key is delivered immediately).
 */
void keyboard_init(void) {
    setvbuf(stdin, NULL, _IONBF, 0);
    /*
     * Use "cbreak" (not "raw") so:
     *   - Ctrl+C still fires SIGINT for the signal handler in main.c
     *   - VS Code integrated terminal doesn't break
     * "-icanon min 0 time 0" makes getchar() non-blocking.
     */
    system("stty cbreak -echo -icanon min 0 time 0");
}

/*
 * keyboard_restore()
 *   Restores the terminal to normal (cooked) behaviour.
 *   Called on every exit path — normal quit, SIGINT, SIGTERM, panic.
 */
void keyboard_restore(void) {
    system("stty sane");
}

/*
 * keyPressed()
 *   Non-blocking single-character read with arrow key and Ctrl+C support.
 *
 *   Returns '\0' if no key is waiting (non-blocking).
 *   Returns 'q'  if Ctrl+C (ASCII 0x03) is detected — software fallback
 *               for terminals where SIGINT is not delivered in cbreak mode.
 *   Returns 'w'/'s'/'a'/'d' for arrow keys (remapped for game use).
 */
char keyPressed(void) {
    int ch = getchar();
    if (ch == EOF) {
        clearerr(stdin);
        return '\0';
    }

    /*
     * Ctrl+C fallback (ASCII ETX = 0x03).
     * In VS Code's terminal, cbreak mode may not deliver SIGINT.
     * Treating 0x03 as 'q' ensures the game can always be quit cleanly
     * and keyboard_restore() will run.
     */
    if (ch == 0x03) return 'q';

    /* Arrow key escape sequence: ESC [ A/B/C/D */
    if (ch == '\033') {
        int seq1 = getchar();
        if (seq1 == EOF) { clearerr(stdin); return '\0'; }
        if (seq1 == '[') {
            int seq2 = getchar();
            if (seq2 == EOF) { clearerr(stdin); return '\0'; }
            switch (seq2) {
                case 'A': return 'w';   /* Up    → rotate   */
                case 'B': return 's';   /* Down  → soft drop */
                case 'C': return 'd';   /* Right → move right */
                case 'D': return 'a';   /* Left  → move left  */
            }
        }
        return '\0';
    }

    return (char)ch;
}

/*
 * readLine(buffer, max_len)
 *   Blocking line read used for menus and name entry (not the game loop).
 *   Temporarily restores cooked mode so backspace and echo work normally,
 *   then re-enters raw/cbreak mode when done.
 */
void readLine(char *buffer, int max_len) {
    if (!buffer || max_len <= 0) return;

    /* Restore normal terminal for comfortable name typing */
    system("stty sane");

    int i = 0;
    while (i < max_len - 1) {
        int ch = getchar();
        if (ch == '\n' || ch == EOF) break;
        buffer[i++] = (char)ch;
    }
    buffer[i] = '\0';

    /* Re-enter non-blocking mode for the game loop */
    keyboard_init();
}

/* ========================== WEBSOCKET MODE ================================ */

static int ws_fd = -1;

typedef struct {
    unsigned char data[8192];
    int len;
} WsBuffer;

static WsBuffer ws_buf;

static void kb_mem_move(unsigned char *dst, const unsigned char *src, int n) {
    int i;
    if (dst < src) {
        for (i = 0; i < n; i++) dst[i] = src[i];
    } else if (dst > src) {
        for (i = n - 1; i >= 0; i--) dst[i] = src[i];
    }
}

void keyboard_init_ws(int socket_fd) {
    ws_fd      = socket_fd;
    ws_buf.len = 0;
}

int keyboard_poll_ws(int timeout_ms) {
    if (ws_fd < 0) return -1;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(ws_fd, &rfds);
    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = timeout_ms * 1000;
    int ret = select(ws_fd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }
    return (ret > 0 && FD_ISSET(ws_fd, &rfds)) ? 1 : 0;
}

int keyboard_recv_ws(char *out, int out_sz) {
    if (ws_fd < 0) return -1;

    int n = (int)recv(ws_fd, ws_buf.data + ws_buf.len,
                      (int)sizeof(ws_buf.data) - ws_buf.len, 0);
    if (n == 0)  return -1;
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    ws_buf.len += n;

    if (ws_buf.len < 2) return 0;

    int fin         = (ws_buf.data[0] & 0x80) != 0;
    int opcode      = ws_buf.data[0] & 0x0F;
    int masked      = (ws_buf.data[1] & 0x80) != 0;
    unsigned long long payload_len = (unsigned long long)(ws_buf.data[1] & 0x7F);
    int index       = 2;

    if (!fin)          return 0;   /* ignore fragmented */
    if (opcode == 0x8) return -1;  /* close frame */
    if (opcode != 0x1) return 0;   /* text frames only */

    if (payload_len == 126) {
        if (ws_buf.len < 4) return 0;
        payload_len = (unsigned long long)(ws_buf.data[2] << 8 | ws_buf.data[3]);
        index = 4;
    } else if (payload_len == 127) {
        return -1;   /* reject 64-bit lengths */
    }

    if (!masked) return -1;
    if (ws_buf.len < index + 4 + (int)payload_len) return 0;

    unsigned char mask[4];
    for (int i = 0; i < 4; i++) mask[i] = ws_buf.data[index + i];
    index += 4;

    int copy_len = (payload_len < (unsigned long long)(out_sz - 1))
                   ? (int)payload_len : (out_sz - 1);
    for (int i = 0; i < copy_len; i++)
        out[i] = (char)(ws_buf.data[index + i] ^ mask[i % 4]);
    out[copy_len] = '\0';

    int frame_size = index + (int)payload_len;
    int remaining  = ws_buf.len - frame_size;
    if (remaining > 0)
        kb_mem_move(ws_buf.data, ws_buf.data + frame_size, remaining);
    ws_buf.len = remaining;
    return copy_len;
}

void keyboard_close_ws(void) {
    if (ws_fd >= 0) {
        close(ws_fd);
        ws_fd = -1;
    }
}