/* =============================================================================
 * screen.c  —  User Interface / Display Output Module
 * =============================================================================
 * OS MODULE: I/O Management (Output side) + User Interface
 *   Abstracts every pixel/character drawn — analogous to a framebuffer driver.
 *   Supports TWO output modes:
 *     1. Terminal mode — ANSI escape sequences to the local terminal.
 *     2. WebSocket mode — WebSocket text frames to a browser client.
 *
 * RULES COMPLIANCE:
 *   - <stdio.h> : allowed for terminal I/O (putchar, fflush).
 *   - Networking headers (<sys/socket.h>, <arpa/inet.h>, <unistd.h>) are used
 *     ONLY for WebSocket mode — Hardware Abstraction Exception (Rule 3).
 *     The socket is our "screen" for the browser client, just as stdout
 *     is the display for the terminal client.
 *   - No printf, no <string.h>, no <math.h>.
 * =============================================================================
 */

#include "../include/screen.h"
#include "../include/t_string.h"
#include <stdio.h>     /* putchar(), fflush() — terminal I/O, allowed */
#include <sys/ioctl.h> /* ioctl(), TIOCGWINSZ — terminal size query */

/* ---- Hardware Abstraction headers for WebSocket mode --------------------- */
#include <arpa/inet.h>  /* htons(), sockaddr_in — network address setup     */
#include <netinet/in.h> /* INADDR_ANY, AF_INET — socket address family      */
#include <sys/socket.h> /* socket(), bind(), listen(), accept(), send()     */
#include <unistd.h>     /* write(), close() — low-level I/O                 */

/* ========================== TERMINAL MODE ================================= */

/* Internal helper: write an integer to stdout without printf. */
static void write_escape_num(int n) {
  if (n == 0) {
    putchar('0');
    return;
  }
  char buf[12];
  int len = 0;
  while (n > 0) {
    buf[len++] = (char)('0' + (n % 10));
    n /= 10;
  }
  int i = len - 1;
  while (i >= 0) {
    putchar(buf[i]);
    i--;
  }
}

void screen_clear(void) {
  putchar('\033');
  putchar('[');
  putchar('2');
  putchar('J');
  putchar('\033');
  putchar('[');
  putchar('H');
  fflush(stdout);
}

void screen_set_cursor(int x, int y) {
  if (x < 1)
    x = 1;
  if (y < 1)
    y = 1;
  putchar('\033');
  putchar('[');
  write_escape_num(y);
  putchar(';');
  write_escape_num(x);
  putchar('H');
  fflush(stdout);
}

void screen_render_char(char c) {
  putchar(c);
  fflush(stdout);
}

void screen_render_string(const char *str) {
  if (!str)
    return;
  int i = 0;
  while (str[i] != '\0') {
    putchar(str[i]);
    i++;
  }
  fflush(stdout);
}

void screen_set_color(int fg, int bg) {
  putchar('\033');
  putchar('[');
  if (fg == 0 && bg == 0) {
    putchar('0');
  } else {
    if (fg > 0)
      write_escape_num(fg);
    if (bg > 0) {
      putchar(';');
      write_escape_num(bg);
    }
  }
  putchar('m');
  fflush(stdout);
}

void screen_reset_color(void) {
  putchar('\033');
  putchar('[');
  putchar('0');
  putchar('m');
  fflush(stdout);
}

void screen_render_int(int value) {
  if (value < 0) {
    putchar('-');
    value = -value;
  }
  write_escape_num(value);
  fflush(stdout);
}

void screen_hide_cursor(void) {
  putchar('\033');
  putchar('[');
  putchar('?');
  putchar('2');
  putchar('5');
  putchar('l');
  fflush(stdout);
}

void screen_show_cursor(void) {
  putchar('\033');
  putchar('[');
  putchar('?');
  putchar('2');
  putchar('5');
  putchar('h');
  fflush(stdout);
}

void screen_get_size(int *cols, int *rows) {
  struct winsize ws;
  if (ioctl(1, TIOCGWINSZ, &ws) == 0) {
    if (cols)
      *cols = ws.ws_col;
    if (rows)
      *rows = ws.ws_row;
  } else {
    /* fallback defaults */
    if (cols)
      *cols = 80;
    if (rows)
      *rows = 24;
  }
}

/* ========================== WEBSOCKET MODE =================================
 */

/* Internal state for the WebSocket output device. */
static int ws_out_fd = -1;

/* ---------------------------------------------------------------------------
 * SHA1 + Base64 (minimal, used only for WebSocket handshake)
 *
 * Uses unsigned char / unsigned int / unsigned long long instead of
 * <stdint.h> types to remain compliant with project rules.
 * ---------------------------------------------------------------------------
 */

typedef struct {
  unsigned int h[5];
  unsigned long long len;
  unsigned char buf[64];
  unsigned int buf_len;
} Sha1;

static unsigned int sha1_rol(unsigned int value, int bits) {
  return (value << bits) | (value >> (32 - bits));
}

static void sha1_init(Sha1 *s) {
  s->h[0] = 0x67452301;
  s->h[1] = 0xEFCDAB89;
  s->h[2] = 0x98BADCFE;
  s->h[3] = 0x10325476;
  s->h[4] = 0xC3D2E1F0;
  s->len = 0;
  s->buf_len = 0;
}

static void sha1_block(Sha1 *s, const unsigned char *block) {
  unsigned int w[80];
  int i;
  for (i = 0; i < 16; i++) {
    w[i] = ((unsigned int)block[i * 4] << 24) |
           ((unsigned int)block[i * 4 + 1] << 16) |
           ((unsigned int)block[i * 4 + 2] << 8) |
           ((unsigned int)block[i * 4 + 3]);
  }
  for (i = 16; i < 80; i++) {
    w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }

  unsigned int a = s->h[0];
  unsigned int b = s->h[1];
  unsigned int c = s->h[2];
  unsigned int d = s->h[3];
  unsigned int e = s->h[4];

  for (i = 0; i < 80; i++) {
    unsigned int f, k;
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }
    unsigned int temp = sha1_rol(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = sha1_rol(b, 30);
    b = a;
    a = temp;
  }

  s->h[0] += a;
  s->h[1] += b;
  s->h[2] += c;
  s->h[3] += d;
  s->h[4] += e;
}

static void sha1_update(Sha1 *s, const unsigned char *data, unsigned int len) {
  s->len += (unsigned long long)len * 8;
  while (len > 0) {
    unsigned int to_copy = 64 - s->buf_len;
    if (to_copy > len)
      to_copy = len;
    for (unsigned int i = 0; i < to_copy; i++) {
      s->buf[s->buf_len + i] = data[i];
    }
    s->buf_len += to_copy;
    data += to_copy;
    len -= to_copy;
    if (s->buf_len == 64) {
      sha1_block(s, s->buf);
      s->buf_len = 0;
    }
  }
}

static void sha1_final(Sha1 *s, unsigned char out[20]) {
  s->buf[s->buf_len++] = 0x80;
  if (s->buf_len > 56) {
    while (s->buf_len < 64)
      s->buf[s->buf_len++] = 0x00;
    sha1_block(s, s->buf);
    s->buf_len = 0;
  }
  while (s->buf_len < 56)
    s->buf[s->buf_len++] = 0x00;
  for (int i = 7; i >= 0; i--) {
    s->buf[s->buf_len++] = (unsigned char)((s->len >> (i * 8)) & 0xFF);
  }
  sha1_block(s, s->buf);
  for (int i = 0; i < 5; i++) {
    out[i * 4] = (unsigned char)(s->h[i] >> 24);
    out[i * 4 + 1] = (unsigned char)(s->h[i] >> 16);
    out[i * 4 + 2] = (unsigned char)(s->h[i] >> 8);
    out[i * 4 + 3] = (unsigned char)(s->h[i]);
  }
}

static int base64_encode(const unsigned char *in, int len, char *out) {
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int i = 0, o = 0;
  while (i < len) {
    unsigned int v = 0;
    int bytes = 0;
    for (int j = 0; j < 3; j++) {
      v <<= 8;
      if (i < len) {
        v |= in[i++];
        bytes++;
      }
    }
    out[o++] = table[(v >> 18) & 0x3F];
    out[o++] = table[(v >> 12) & 0x3F];
    out[o++] = (bytes > 1) ? table[(v >> 6) & 0x3F] : '=';
    out[o++] = (bytes > 2) ? table[v & 0x3F] : '=';
  }
  out[o] = '\0';
  return o;
}

/* ---------------------------------------------------------------------------
 * screen_init_ws(socket_fd)
 *   Registers a connected WebSocket client socket as the output device.
 *   Analogous to the terminal being stdout.
 * ---------------------------------------------------------------------------
 */
void screen_init_ws(int socket_fd) { ws_out_fd = socket_fd; }

/* ---------------------------------------------------------------------------
 * screen_ws_handshake()
 *   Performs the HTTP -> WebSocket upgrade handshake on the registered socket.
 *   Returns 0 on success, -1 on failure.
 *
 *   This is the "display initialisation" step for WebSocket mode,
 *   analogous to clearing the terminal screen on startup.
 * ---------------------------------------------------------------------------
 */
int screen_ws_handshake(void) {
  if (ws_out_fd < 0)
    return -1;

  /* Read HTTP request until CRLF CRLF. */
  char req[4096];
  int total = 0;
  while (total < (int)sizeof(req) - 1) {
    int n = (int)recv(ws_out_fd, req + total, sizeof(req) - 1 - total, 0);
    if (n <= 0)
      return -1;
    total += n;
    req[total] = '\0';
    if (total >= 4 && req[total - 4] == '\r' && req[total - 3] == '\n' &&
        req[total - 2] == '\r' && req[total - 1] == '\n') {
      break;
    }
  }

  /* Extract "Sec-WebSocket-Key" header value. */
  const char *key_header = "Sec-WebSocket-Key:";
  int key_hdr_len = t_strlen(key_header);
  int key_start = -1;
  for (int i = 0; i + key_hdr_len < total; i++) {
    int match = 1;
    for (int j = 0; j < key_hdr_len; j++) {
      if (req[i + j] != key_header[j]) {
        match = 0;
        break;
      }
    }
    if (match) {
      key_start = i + key_hdr_len;
      break;
    }
  }
  if (key_start < 0)
    return -1;

  while (key_start < total && (req[key_start] == ' ' || req[key_start] == '\t'))
    key_start++;

  char key[128];
  int k = 0;
  while (key_start < total && req[key_start] != '\r' &&
         req[key_start] != '\n' && k < (int)sizeof(key) - 1) {
    key[k++] = req[key_start++];
  }
  key[k] = '\0';

  /* RFC6455 GUID. */
  const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  char concat[256];
  int concat_len = 0;
  for (int i = 0; key[i] && concat_len < (int)sizeof(concat) - 1; i++)
    concat[concat_len++] = key[i];
  for (int i = 0; guid[i] && concat_len < (int)sizeof(concat) - 1; i++)
    concat[concat_len++] = guid[i];
  concat[concat_len] = '\0';

  Sha1 s;
  unsigned char digest[20];
  sha1_init(&s);
  sha1_update(&s, (const unsigned char *)concat, (unsigned int)concat_len);
  sha1_final(&s, digest);

  char accept[64];
  base64_encode(digest, 20, accept);

  /* Send protocol-switch response. */
  char resp[256];
  int resp_len = 0;
  const char *p1 = "HTTP/1.1 101 Switching Protocols\r\n";
  const char *p2 = "Upgrade: websocket\r\n";
  const char *p3 = "Connection: Upgrade\r\n";
  const char *p4 = "Sec-WebSocket-Accept: ";
  const char *p5 = "\r\n\r\n";

  for (int i = 0; p1[i] && resp_len < (int)sizeof(resp) - 1; i++)
    resp[resp_len++] = p1[i];
  for (int i = 0; p2[i] && resp_len < (int)sizeof(resp) - 1; i++)
    resp[resp_len++] = p2[i];
  for (int i = 0; p3[i] && resp_len < (int)sizeof(resp) - 1; i++)
    resp[resp_len++] = p3[i];
  for (int i = 0; p4[i] && resp_len < (int)sizeof(resp) - 1; i++)
    resp[resp_len++] = p4[i];
  for (int i = 0; accept[i] && resp_len < (int)sizeof(resp) - 1; i++)
    resp[resp_len++] = accept[i];
  for (int i = 0; p5[i] && resp_len < (int)sizeof(resp) - 1; i++)
    resp[resp_len++] = p5[i];

  if (resp_len <= 0)
    return -1;
  if (send(ws_out_fd, resp, resp_len, 0) < 0)
    return -1;
  return 0;
}

/* ---------------------------------------------------------------------------
 * screen_send_ws(json)
 *   Sends a WebSocket text frame containing the given JSON string.
 *   Returns bytes sent on success, -1 on error.
 *
 *   This is the WebSocket equivalent of screen_render_string().
 * ---------------------------------------------------------------------------
 */
int screen_send_ws(const char *json) {
  if (ws_out_fd < 0 || !json)
    return -1;

  int len = t_strlen(json);
  unsigned char header[4];
  int header_len = 2;
  header[0] = 0x81; /* FIN + text opcode */

  if (len <= 125) {
    header[1] = (unsigned char)len;
  } else if (len <= 65535) {
    header[1] = 126;
    header[2] = (unsigned char)((len >> 8) & 0xFF);
    header[3] = (unsigned char)(len & 0xFF);
    header_len = 4;
  } else {
    return -1;
  }

  if (send(ws_out_fd, header, header_len, 0) < 0)
    return -1;
  return (int)send(ws_out_fd, json, len, 0);
}

/* ---------------------------------------------------------------------------
 * screen_server_listen(port)
 *   Creates a TCP server socket, binds to the given port, and starts
 *   listening.  Returns the server file descriptor on success, -1 on error.
 *   The caller should later call screen_server_accept() to accept a client.
 * ---------------------------------------------------------------------------
 */
int screen_server_listen(int port) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
    return -1;

  /* Allow quick restart without waiting for TIME_WAIT. */
  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((unsigned short)port);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(server_fd);
    return -1;
  }
  if (listen(server_fd, 1) < 0) {
    close(server_fd);
    return -1;
  }
  return server_fd;
}

/* ---------------------------------------------------------------------------
 * screen_server_accept(server_fd)
 *   Accepts a single client connection on the listening socket.
 *   Returns the client file descriptor on success, -1 on error.
 * ---------------------------------------------------------------------------
 */
int screen_server_accept(int srv_fd) {
  if (srv_fd < 0)
    return -1;
  return accept(srv_fd, NULL, NULL);
}

/* ---------------------------------------------------------------------------
 * screen_server_close(server_fd)
 *   Closes the server socket.
 * ---------------------------------------------------------------------------
 */
void screen_server_close(int srv_fd) {
  if (srv_fd >= 0)
    close(srv_fd);
}

/* ---------------------------------------------------------------------------
 * screen_server_start(port)
 *   Convenience wrapper: listen + accept in one call.
 *   Closes the server socket after accepting (legacy behaviour).
 * ---------------------------------------------------------------------------
 */
int screen_server_start(int port) {
  int srv = screen_server_listen(port);
  if (srv < 0)
    return -1;
  int client_fd = screen_server_accept(srv);
  if (client_fd < 0) {
    close(srv);
    return -1;
  }
  close(srv);
  return client_fd;
}
