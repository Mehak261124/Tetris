/* =============================================================================
 * keyboard.c  —  Input/Output Management Module
 * =============================================================================
 * OS MODULE: I/O Management
 *   Manages how this "computer" (our Tetris OS) talks to the world.
 *   Supports TWO input modes:
 *     1. Terminal mode — raw, non-blocking key reads from stdin.
 *     2. WebSocket mode — non-blocking message reads from a socket client.
 *
 * RULES COMPLIANCE:
 *   - <stdio.h>  : allowed (terminal I/O only — getchar / EOF)
 *   - <stdlib.h>  : allowed (system() for stty terminal control)
 *   - Networking headers (<sys/socket.h>, <errno.h>, <unistd.h>) are used
 *     ONLY for WebSocket mode — Hardware Abstraction Exception (Rule 3).
 *     The socket is our "keyboard" for the browser client, just as stdin
 *     is the keyboard for the terminal client.
 * =============================================================================
 */

#include "../include/keyboard.h"
#include <stdio.h>  /* getchar(), EOF — terminal I/O exception */
#include <stdlib.h> /* system() — process control exception    */

/* ---- Hardware Abstraction headers for WebSocket mode --------------------- */
#include <errno.h>      /* EAGAIN / EWOULDBLOCK for non-blocking recv     */
#include <sys/socket.h> /* recv() — reading from the browser "keyboard"   */
#include <sys/time.h>   /* struct timeval, select() — input polling        */
#include <unistd.h>     /* close() — releasing the socket device          */

/* ========================== TERMINAL MODE ================================= */

/* ---------------------------------------------------------------------------
 * keyboard_init()
 *   Switches the terminal into "raw" mode so every keypress is delivered
 *   immediately to our game loop without the OS line-buffering it.
 * ---------------------------------------------------------------------------
 */
void keyboard_init(void) {
  setvbuf(stdin, NULL, _IONBF, 0);
  system("stty raw -echo -icanon min 0 time 0");
}

/* ---------------------------------------------------------------------------
 * keyboard_restore()
 *   Restores the terminal to normal (cooked) behaviour.
 * ---------------------------------------------------------------------------
 */
void keyboard_restore(void) { system("stty sane"); }

/* ---------------------------------------------------------------------------
 * keyPressed()
 *   Non-blocking single-character read with arrow key support.
 * ---------------------------------------------------------------------------
 */
char keyPressed(void) {
  int ch = getchar();
  if (ch == EOF) {
    clearerr(stdin);
    return '\0';
  }

  if (ch == '\033') {
    int seq1 = getchar();
    if (seq1 == EOF) {
      clearerr(stdin);
      return '\0';
    }
    if (seq1 == '[') {
      int seq2 = getchar();
      if (seq2 == EOF) {
        clearerr(stdin);
        return '\0';
      }
      switch (seq2) {
      case 'A':
        return 'w';
      case 'B':
        return 's';
      case 'C':
        return 'd';
      case 'D':
        return 'a';
      }
    }
    return '\0';
  }

  return (char)ch;
}

/* ---------------------------------------------------------------------------
 * readLine(buffer, max_len)
 *   Blocking line read (used for menus / name entry, not the game loop).
 * ---------------------------------------------------------------------------
 */
void readLine(char *buffer, int max_len) {
  if (!buffer || max_len <= 0)
    return;

  system("stty sane");

  int i = 0;
  while (i < max_len - 1) {
    int ch = getchar();
    if (ch == '\n' || ch == EOF)
      break;
    buffer[i++] = (char)ch;
  }
  buffer[i] = '\0';

  keyboard_init();
}

/* ========================== WEBSOCKET MODE =================================
 */

/* Internal state for WebSocket input device. */
static int ws_fd = -1;

/* Receive buffer: accumulates raw bytes from the socket between calls. */
typedef struct {
  unsigned char data[8192];
  int len;
} WsBuffer;

static WsBuffer ws_buf;

/* Custom byte-move helper (no <string.h>). */
static void kb_mem_move(unsigned char *dst, const unsigned char *src, int n) {
  int i;
  if (dst < src) {
    for (i = 0; i < n; i++)
      dst[i] = src[i];
  } else if (dst > src) {
    for (i = n - 1; i >= 0; i--)
      dst[i] = src[i];
  }
}

/* ---------------------------------------------------------------------------
 * keyboard_init_ws(socket_fd)
 *   Registers a connected WebSocket client socket as the input device.
 *   Analogous to keyboard_init() for terminal mode.
 * ---------------------------------------------------------------------------
 */
void keyboard_init_ws(int socket_fd) {
  ws_fd = socket_fd;
  ws_buf.len = 0;
}

/* ---------------------------------------------------------------------------
 * keyboard_poll_ws(timeout_ms)
 *   Polls the WebSocket input device for readability, waiting up to
 *   timeout_ms milliseconds.
 *   Returns:
 *     1  if data is ready to read (call keyboard_recv_ws next)
 *     0  if timeout elapsed with no data
 *    -1  on error
 *
 *   This is the WebSocket equivalent of checking if a key was pressed
 *   within a game-tick interval.
 * ---------------------------------------------------------------------------
 */
int keyboard_poll_ws(int timeout_ms) {
  if (ws_fd < 0)
    return -1;
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(ws_fd, &rfds);
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = timeout_ms * 1000;
  int ret = select(ws_fd + 1, &rfds, NULL, NULL, &tv);
  if (ret < 0) {
    if (errno == EINTR)
      return 0;
    return -1;
  }
  return (ret > 0 && FD_ISSET(ws_fd, &rfds)) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * keyboard_recv_ws(out, out_sz)
 *   Non-blocking read of one WebSocket text message.
 *   Decodes the WebSocket frame (masked, opcode 0x1) and writes the
 *   unmasked payload into `out`.
 *
 *   Returns:
 *     >0  message length written to out
 *      0  no complete message yet (try again next tick)
 *     -1  connection closed or protocol error
 *
 *   This is the WebSocket equivalent of keyPressed().
 * ---------------------------------------------------------------------------
 */
int keyboard_recv_ws(char *out, int out_sz) {
  if (ws_fd < 0)
    return -1;

  /* Accumulate bytes from socket. */
  int n = (int)recv(ws_fd, ws_buf.data + ws_buf.len,
                    (int)sizeof(ws_buf.data) - ws_buf.len, 0);
  if (n == 0)
    return -1;
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return 0;
    return -1;
  }
  ws_buf.len += n;

  /* Need at least 2 bytes for the frame header. */
  if (ws_buf.len < 2)
    return 0;

  int fin = (ws_buf.data[0] & 0x80) != 0;
  int opcode = ws_buf.data[0] & 0x0F;
  int masked = (ws_buf.data[1] & 0x80) != 0;
  unsigned long long payload_len = (unsigned long long)(ws_buf.data[1] & 0x7F);
  int index = 2;

  if (!fin)
    return 0; /* ignore fragmented messages */
  if (opcode == 0x8)
    return -1; /* close frame */
  if (opcode != 0x1)
    return 0; /* only text frames */

  if (payload_len == 126) {
    if (ws_buf.len < 4)
      return 0;
    payload_len = (unsigned long long)(ws_buf.data[2] << 8 | ws_buf.data[3]);
    index = 4;
  } else if (payload_len == 127) {
    return -1; /* reject 64-bit lengths */
  }

  if (!masked)
    return -1; /* RFC: client frames must be masked */
  if (ws_buf.len < index + 4 + (int)payload_len)
    return 0;

  /* Unmask payload. */
  unsigned char mask[4];
  for (int i = 0; i < 4; i++)
    mask[i] = ws_buf.data[index + i];
  index += 4;

  int copy_len = (payload_len < (unsigned long long)(out_sz - 1))
                     ? (int)payload_len
                     : (out_sz - 1);
  for (int i = 0; i < copy_len; i++) {
    out[i] = (char)(ws_buf.data[index + i] ^ mask[i % 4]);
  }
  out[copy_len] = '\0';

  /* Remove consumed frame; keep any trailing bytes. */
  int frame_size = index + (int)payload_len;
  int remaining = ws_buf.len - frame_size;
  if (remaining > 0) {
    kb_mem_move(ws_buf.data, ws_buf.data + frame_size, remaining);
  }
  ws_buf.len = remaining;
  return copy_len;
}

/* ---------------------------------------------------------------------------
 * keyboard_close_ws()
 *   Closes the WebSocket input device.
 *   Analogous to keyboard_restore() for terminal mode.
 * ---------------------------------------------------------------------------
 */
void keyboard_close_ws(void) {
  if (ws_fd >= 0) {
    close(ws_fd);
    ws_fd = -1;
  }
}
