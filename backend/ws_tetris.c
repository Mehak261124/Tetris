/* =============================================================================
 * ws_tetris.c  —  WebSocket Backend for Browser Tetris
 * =============================================================================
 * ROLE:
 *   Runs a minimal single-client game server that speaks plain WebSocket
 *   frames and streams JSON snapshots to the React UI.
 *
 * HIGH-LEVEL FLOW:
 *   1) Accept TCP client on :8080
 *   2) Perform HTTP -> WebSocket upgrade handshake
 *   3) Enter fixed-tick game loop
 *      - read action messages from client
 *      - apply Tetris logic (move/rotate/drop/pause/restart)
 *      - advance gravity over time
 *      - emit updated JSON state on changes
 *
 * DESIGN CHOICES:
 *   - Single-client only (simpler state ownership and no room synchronization).
 *   - No external JSON / WS libraries (everything done manually in C).
 *   - Strict frame subset: text frames only, limited payload sizes.
 * ============================================================================= */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PORT_DEFAULT 8080
#define ROWS 20
#define COLS 10
#define NUM_PIECES 7
#define NUM_ROTATIONS 4
#define PIECE_SIZE 4

/* Active piece coordinates are board-space cell indices, not pixels. */
typedef struct {
    int type;
    int rotation;
    int x;
    int y;
} Piece;

/* Entire mutable game state for one running session. */
typedef struct {
    int board[ROWS][COLS];
    Piece current;
    Piece next;
    int has_current;
    int score;
    int level;
    int lines;
    int game_over;
    int paused;
    int rand_state;
} Game;

/* Tetromino lookup table: [piece][rotation][row][col] -> 0/1 occupancy. */
static const int PIECES[NUM_PIECES][NUM_ROTATIONS][PIECE_SIZE][PIECE_SIZE] = {
    /* I */
    {
        {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
        {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}
    },
    /* O */
    {
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
    },
    /* T */
    {
        {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    /* S */
    {
        {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
        {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}
    },
    /* Z */
    {
        {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}
    },
    /* J */
    {
        {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}
    },
    /* L */
    {
        {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
        {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
        {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
        {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}
    }
};

static const char PIECE_NAMES[NUM_PIECES] = {'I','O','T','S','Z','J','L'};

/* Tiny string helpers to keep this file fully self-contained. */
static int str_len(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int str_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void mem_move(unsigned char *dst, const unsigned char *src, int n) {
    int i;
    if (dst < src) {
        for (i = 0; i < n; i++) dst[i] = src[i];
    } else if (dst > src) {
        for (i = n - 1; i >= 0; i--) dst[i] = src[i];
    }
}

/* ---------------- SHA1 + Base64 (minimal) ----------------
 * Used only for WebSocket handshake:
 *   accept = Base64( SHA1(client_key + GUID) )
 */
typedef struct {
    uint32_t h[5];
    uint64_t len;
    uint8_t  buf[64];
    uint32_t buf_len;
} Sha1;

static uint32_t rol(uint32_t value, int bits) {
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

static void sha1_block(Sha1 *s, const uint8_t *block) {
    uint32_t w[80];
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }
    for (i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = s->h[0];
    uint32_t b = s->h[1];
    uint32_t c = s->h[2];
    uint32_t d = s->h[3];
    uint32_t e = s->h[4];

    for (i = 0; i < 80; i++) {
        uint32_t f, k;
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
        uint32_t temp = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = temp;
    }

    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
}

static void sha1_update(Sha1 *s, const uint8_t *data, uint32_t len) {
    s->len += (uint64_t)len * 8;
    while (len > 0) {
        uint32_t to_copy = 64 - s->buf_len;
        if (to_copy > len) to_copy = len;
        for (uint32_t i = 0; i < to_copy; i++) {
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

static void sha1_final(Sha1 *s, uint8_t out[20]) {
    s->buf[s->buf_len++] = 0x80;
    if (s->buf_len > 56) {
        while (s->buf_len < 64) s->buf[s->buf_len++] = 0x00;
        sha1_block(s, s->buf);
        s->buf_len = 0;
    }
    while (s->buf_len < 56) s->buf[s->buf_len++] = 0x00;
    for (int i = 7; i >= 0; i--) {
        s->buf[s->buf_len++] = (uint8_t)((s->len >> (i * 8)) & 0xFF);
    }
    sha1_block(s, s->buf);
    for (int i = 0; i < 5; i++) {
        out[i * 4]     = (uint8_t)(s->h[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(s->h[i]);
    }
}

static int base64_encode(const uint8_t *in, int len, char *out) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, o = 0;
    while (i < len) {
        uint32_t v = 0;
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

static int websocket_handshake(int fd) {
    /* Read HTTP request until CRLF CRLF (end of headers). */
    char req[4096];
    int total = 0;
    while (total < (int)sizeof(req) - 1) {
        int n = (int)recv(fd, req + total, sizeof(req) - 1 - total, 0);
        if (n <= 0) return -1;
        total += n;
        req[total] = '\0';
        if (total >= 4 &&
            req[total - 4] == '\r' && req[total - 3] == '\n' &&
            req[total - 2] == '\r' && req[total - 1] == '\n') {
            break;
        }
    }

    /* Extract "Sec-WebSocket-Key" header value. */
    const char *key_header = "Sec-WebSocket-Key:";
    int key_len = str_len(key_header);
    int key_start = -1;
    for (int i = 0; i + key_len < total; i++) {
        int match = 1;
        for (int j = 0; j < key_len; j++) {
            if (req[i + j] != key_header[j]) { match = 0; break; }
        }
        if (match) { key_start = i + key_len; break; }
    }
    if (key_start < 0) return -1;

    while (key_start < total && (req[key_start] == ' ' || req[key_start] == '\t')) {
        key_start++;
    }

    char key[128];
    int k = 0;
    while (key_start < total && req[key_start] != '\r' && req[key_start] != '\n' && k < (int)sizeof(key) - 1) {
        key[k++] = req[key_start++];
    }
    key[k] = '\0';

    /* RFC6455 fixed GUID appended before hashing. */
    const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concat[256];
    int concat_len = 0;
    for (int i = 0; key[i] && concat_len < (int)sizeof(concat) - 1; i++) concat[concat_len++] = key[i];
    for (int i = 0; guid[i] && concat_len < (int)sizeof(concat) - 1; i++) concat[concat_len++] = guid[i];
    concat[concat_len] = '\0';

    Sha1 s;
    uint8_t digest[20];
    sha1_init(&s);
    sha1_update(&s, (const uint8_t *)concat, (uint32_t)concat_len);
    sha1_final(&s, digest);

    char accept[64];
    base64_encode(digest, 20, accept);

    /* Send protocol-switch response to complete upgrade. */
    char resp[256];
    int resp_len = snprintf(
        resp,
        sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept
    );
    if (resp_len <= 0) return -1;
    return (int)send(fd, resp, resp_len, 0);
}

/* ---------------- WebSocket frame IO ----------------
 * Client -> server:
 *   - must be masked
 *   - we accept FIN text frames (opcode 0x1) and close (0x8)
 *
 * Server -> client:
 *   - sends unmasked FIN text frames
 */
typedef struct {
    unsigned char data[8192];
    int len;
} WsBuffer;

/*
 * Reads one client->server text message from an internal byte buffer.
 * Returns:
 *  >0  message length copied to out
 *   0  need more bytes / no complete frame yet
 *  -1  closed socket or protocol error
 */
static int ws_read_message(int fd, WsBuffer *buf, char *out, int out_sz) {
    /* Accumulate bytes; caller invokes this every tick. */
    int n = (int)recv(fd, buf->data + buf->len, (int)sizeof(buf->data) - buf->len, 0);
    if (n == 0) return -1;
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    buf->len += n;

    if (buf->len < 2) return 0;
    int fin = (buf->data[0] & 0x80) != 0;
    int opcode = buf->data[0] & 0x0F;
    int masked = (buf->data[1] & 0x80) != 0;
    uint64_t payload_len = (uint64_t)(buf->data[1] & 0x7F);
    int index = 2;

    /* This minimal server ignores fragmented messages. */
    if (!fin) return 0;
    if (opcode == 0x8) return -1;
    if (opcode != 0x1) return 0;

    /* Support small/medium payloads; reject 64-bit lengths for simplicity. */
    if (payload_len == 126) {
        if (buf->len < 4) return 0;
        payload_len = (uint64_t)(buf->data[2] << 8 | buf->data[3]);
        index = 4;
    } else if (payload_len == 127) {
        return -1;
    }

    /* Per RFC, browser->server frames must be masked. */
    if (!masked) return -1;
    if (buf->len < index + 4 + (int)payload_len) return 0;

    /* Apply mask key cyclically to reconstruct original payload bytes. */
    unsigned char mask[4];
    for (int i = 0; i < 4; i++) mask[i] = buf->data[index + i];
    index += 4;

    int copy_len = (payload_len < (uint64_t)(out_sz - 1)) ? (int)payload_len : (out_sz - 1);
    for (int i = 0; i < copy_len; i++) {
        out[i] = (char)(buf->data[index + i] ^ mask[i % 4]);
    }
    out[copy_len] = '\0';

    /* Remove consumed frame bytes; preserve any trailing buffered bytes. */
    int frame_size = index + (int)payload_len;
    int remaining = buf->len - frame_size;
    if (remaining > 0) {
        mem_move(buf->data, buf->data + frame_size, remaining);
    }
    buf->len = remaining;
    return copy_len;
}

static int ws_send_text(int fd, const char *msg) {
    int len = str_len(msg);
    unsigned char header[4];
    int header_len = 2;
    header[0] = 0x81;
    /* Server->client frames are not masked. */
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
    if (send(fd, header, header_len, 0) < 0) return -1;
    return (int)send(fd, msg, len, 0);
}

/* ---------------- Game logic ---------------- */
/* LCG pseudo-random generator; deterministic from seed, enough for gameplay. */
static int rand_next(Game *g) {
    g->rand_state = (g->rand_state * 1103515245 + 12345) & 0x7FFFFFFF;
    return g->rand_state % NUM_PIECES;
}

static void seed_rand(Game *g) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    g->rand_state = (int)(tv.tv_usec ^ tv.tv_sec);
    if (g->rand_state == 0) g->rand_state = 1;
}

static void clear_board(Game *g) {
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            g->board[r][c] = 0;
        }
    }
}

static void game_reset(Game *g) {
    clear_board(g);
    g->score = 0;
    g->level = 1;
    g->lines = 0;
    g->paused = 0;
    g->game_over = 0;
    g->has_current = 0;
    seed_rand(g);
    /* Keep one "next piece" ready so UI can render preview immediately. */
    g->next.type = rand_next(g);
    g->next.rotation = 0;
    g->next.x = 3;
    g->next.y = 0;
}

static int collides(Game *g, Piece *p, int dy, int dx, int rot) {
    for (int r = 0; r < PIECE_SIZE; r++) {
        for (int c = 0; c < PIECE_SIZE; c++) {
            if (!PIECES[p->type][rot][r][c]) continue;
            int nr = p->y + dy + r;
            int nc = p->x + dx + c;
            /* Cells above the board (nr < 0) are allowed while spawning/rotating. */
            if (nc < 0 || nc >= COLS || nr >= ROWS) return 1;
            if (nr >= 0 && g->board[nr][nc] > 0) return 1;
        }
    }
    return 0;
}

static void spawn_piece(Game *g) {
    /* Promote queued piece -> active piece, then roll a fresh "next". */
    g->current = g->next;
    g->current.rotation = 0;
    g->current.x = 3;
    g->current.y = 0;
    g->has_current = 1;

    g->next.type = rand_next(g);
    g->next.rotation = 0;
    g->next.x = 3;
    g->next.y = 0;

    if (collides(g, &g->current, 0, 0, g->current.rotation)) {
        g->game_over = 1;
    }
}

static int lock_piece(Game *g) {
    int overflow = 0;
    for (int r = 0; r < PIECE_SIZE; r++) {
        for (int c = 0; c < PIECE_SIZE; c++) {
            if (!PIECES[g->current.type][g->current.rotation][r][c]) continue;
            int nr = g->current.y + r;
            int nc = g->current.x + c;
            /* Any locked block above row 0 means stack overflow -> game over. */
            if (nr < 0) {
                overflow = 1;
            } else if (nr < ROWS && nc >= 0 && nc < COLS) {
                g->board[nr][nc] = g->current.type + 1;
            }
        }
    }
    g->has_current = 0;
    return overflow;
}

static void clear_lines(Game *g) {
    /* Scan bottom-up; when a row clears, shift everything above downward. */
    int cleared = 0;
    for (int r = ROWS - 1; r >= 0; r--) {
        int full = 1;
        for (int c = 0; c < COLS; c++) {
            if (g->board[r][c] == 0) { full = 0; break; }
        }
        if (full) {
            cleared++;
            for (int rr = r; rr > 0; rr--) {
                for (int cc = 0; cc < COLS; cc++) {
                    g->board[rr][cc] = g->board[rr - 1][cc];
                }
            }
            for (int cc = 0; cc < COLS; cc++) g->board[0][cc] = 0;
            /* Re-check same row index because rows shifted down. */
            r++;
        }
    }

    if (cleared > 0) {
        g->lines += cleared;
        /* Classic-ish Tetris line clear scoring scaled by current level. */
        int base = (cleared == 1) ? 100 : (cleared == 2) ? 300 : (cleared == 3) ? 500 : 800;
        g->score += base * g->level;
        g->level = 1 + (g->lines / 10);
    }
}

static void step_down(Game *g) {
    /* Gravity step: move piece down or lock/spawn if blocked. */
    if (!g->has_current) spawn_piece(g);
    if (g->game_over) return;
    if (!collides(g, &g->current, 1, 0, g->current.rotation)) {
        g->current.y += 1;
    } else {
        int overflow = lock_piece(g);
        if (overflow) {
            g->game_over = 1;
            return;
        }
        clear_lines(g);
        spawn_piece(g);
    }
}

static int ghost_y(Game *g) {
    /* Compute projected landing row for current piece. */
    Piece p = g->current;
    while (!collides(g, &p, 1, 0, p.rotation)) {
        p.y += 1;
    }
    return p.y;
}

static void handle_action(Game *g, const char *action) {
    /* Control commands allowed regardless of current gameplay state. */
    if (str_eq(action, "pause")) {
        g->paused = !g->paused;
        return;
    }
    if (str_eq(action, "restart")) {
        game_reset(g);
        return;
    }
    /* Ignore gameplay actions while paused or after game over. */
    if (g->game_over || g->paused) return;

    if (str_eq(action, "move_left")) {
        if (!collides(g, &g->current, 0, -1, g->current.rotation)) g->current.x -= 1;
    } else if (str_eq(action, "move_right")) {
        if (!collides(g, &g->current, 0, 1, g->current.rotation)) g->current.x += 1;
    } else if (str_eq(action, "rotate")) {
        int nr = (g->current.rotation + 1) % NUM_ROTATIONS;
        if (!collides(g, &g->current, 0, 0, nr)) g->current.rotation = nr;
    } else if (str_eq(action, "soft_drop")) {
        /* Only grant soft-drop score if the same piece actually moved down. */
        int old_y = g->current.y;
        int old_type = g->current.type;
        int old_has = g->has_current;
        step_down(g);
        if (g->has_current && old_has && g->current.type == old_type && g->current.y > old_y) {
            g->score += 1;
        }
    } else if (str_eq(action, "hard_drop")) {
        /* Move to landing position immediately and score +2 per moved row. */
        int moved = 0;
        while (!collides(g, &g->current, 1, 0, g->current.rotation)) {
            g->current.y += 1;
            moved++;
        }
        if (moved > 0) g->score += moved * 2;
        int overflow = lock_piece(g);
        if (overflow) {
            g->game_over = 1;
            return;
        }
        clear_lines(g);
        spawn_piece(g);
    }
}

static int parse_action(const char *msg, char *out, int out_sz) {
    const char *needle = "\"action\"";
    /* Minimal parser for messages like: {"action":"move_left"} */
    int i = 0;
    while (msg[i]) {
        int match = 1;
        for (int j = 0; needle[j]; j++) {
            if (msg[i + j] != needle[j]) { match = 0; break; }
        }
        if (match) {
            int k = i;
            while (msg[k] && msg[k] != ':') k++;
            if (!msg[k]) return 0;
            k++;
            while (msg[k] && msg[k] != '"') k++;
            if (!msg[k]) return 0;
            k++;
            int o = 0;
            while (msg[k] && msg[k] != '"' && o < out_sz - 1) {
                out[o++] = msg[k++];
            }
            out[o] = '\0';
            return 1;
        }
        i++;
    }
    return 0;
}

/* ---------------- JSON serialization ----------------
 * Manual JSON writer to avoid external dependencies.
 * Contract intentionally matches what `ui/src/App.jsx` expects.
 */
static void append_char(char *buf, int *len, char c) {
    buf[(*len)++] = c;
}

static void append_str(char *buf, int *len, const char *s) {
    for (int i = 0; s[i]; i++) buf[(*len)++] = s[i];
}

static void append_int(char *buf, int *len, int v) {
    if (v == 0) {
        buf[(*len)++] = '0';
        return;
    }
    if (v < 0) {
        buf[(*len)++] = '-';
        v = -v;
    }
    char tmp[16];
    int t = 0;
    while (v > 0 && t < 15) {
        tmp[t++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (int i = t - 1; i >= 0; i--) buf[(*len)++] = tmp[i];
}

static void append_shape(char *buf, int *len, int type, int rot) {
    /* Emit a 4x4 shape matrix for the requested piece/rotation. */
    append_char(buf, len, '[');
    for (int r = 0; r < PIECE_SIZE; r++) {
        append_char(buf, len, '[');
        for (int c = 0; c < PIECE_SIZE; c++) {
            append_int(buf, len, PIECES[type][rot][r][c]);
            if (c < PIECE_SIZE - 1) append_char(buf, len, ',');
        }
        append_char(buf, len, ']');
        if (r < PIECE_SIZE - 1) append_char(buf, len, ',');
    }
    append_char(buf, len, ']');
}

static void build_state_json(Game *g, char *out, int out_sz) {
    int len = 0;
    append_char(out, &len, '{');

    /* Board cells are encoded as numeric type IDs (0 = empty). */
    append_str(out, &len, "\"board\":[");
    for (int r = 0; r < ROWS; r++) {
        append_char(out, &len, '[');
        for (int c = 0; c < COLS; c++) {
            append_int(out, &len, g->board[r][c]);
            if (c < COLS - 1) append_char(out, &len, ',');
        }
        append_char(out, &len, ']');
        if (r < ROWS - 1) append_char(out, &len, ',');
    }
    append_char(out, &len, ']');

    /* currentPiece and ghostPiece become null during game-over transitions. */
    append_str(out, &len, ",\"currentPiece\":");
    if (g->has_current && !g->game_over) {
        append_char(out, &len, '{');
        append_str(out, &len, "\"shape\":");
        append_shape(out, &len, g->current.type, g->current.rotation);
        append_str(out, &len, ",\"x\":");
        append_int(out, &len, g->current.x);
        append_str(out, &len, ",\"y\":");
        append_int(out, &len, g->current.y);
        append_str(out, &len, ",\"type\":\"");
        append_char(out, &len, PIECE_NAMES[g->current.type]);
        append_char(out, &len, '"');
        append_char(out, &len, '}');
    } else {
        append_str(out, &len, "null");
    }

    /* Ghost piece is a projection of where current piece will land. */
    append_str(out, &len, ",\"ghostPiece\":");
    if (g->has_current && !g->game_over) {
        append_char(out, &len, '{');
        append_str(out, &len, "\"shape\":");
        append_shape(out, &len, g->current.type, g->current.rotation);
        append_str(out, &len, ",\"x\":");
        append_int(out, &len, g->current.x);
        append_str(out, &len, ",\"y\":");
        append_int(out, &len, ghost_y(g));
        append_str(out, &len, ",\"type\":\"");
        append_char(out, &len, PIECE_NAMES[g->current.type]);
        append_char(out, &len, '"');
        append_char(out, &len, '}');
    } else {
        append_str(out, &len, "null");
    }

    append_str(out, &len, ",\"nextPiece\":{");
    append_str(out, &len, "\"shape\":");
    append_shape(out, &len, g->next.type, 0);
    append_str(out, &len, ",\"type\":\"");
    append_char(out, &len, PIECE_NAMES[g->next.type]);
    append_char(out, &len, '"');
    append_char(out, &len, '}');

    append_str(out, &len, ",\"score\":");
    append_int(out, &len, g->score);
    append_str(out, &len, ",\"level\":");
    append_int(out, &len, g->level);
    append_str(out, &len, ",\"lines\":");
    append_int(out, &len, g->lines);

    append_str(out, &len, ",\"state\":\"");
    if (g->game_over) append_str(out, &len, "gameover");
    else if (g->paused) append_str(out, &len, "paused");
    else append_str(out, &len, "playing");
    append_char(out, &len, '"');

    append_char(out, &len, '}');
    /* Hard cap to guarantee NUL-termination. */
    if (len >= out_sz) len = out_sz - 1;
    out[len] = '\0';
}

int main(void) {
    /* Create listening TCP socket for browser clients. */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    /* Allow quick restart without waiting for TIME_WAIT to expire. */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT_DEFAULT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 1) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Tetris WS backend listening on ws://localhost:%d\n", PORT_DEFAULT);

    /* Single-client server: accept one browser connection and drive that session. */
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        return 1;
    }

    if (websocket_handshake(client_fd) < 0) {
        fprintf(stderr, "WebSocket handshake failed\n");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    /* Initialize a fresh game immediately after successful WS upgrade. */
    Game game;
    game_reset(&game);
    spawn_piece(&game);

    WsBuffer buf;
    buf.len = 0;
    char msg[2048];
    char json[8192];

    /*
     * Fixed update cadence:
     * - poll for input every 50ms
     * - gravity speed scales with level via drop_ticks
     */
    int tick_ms = 50;
    int drop_ticks = 12;
    int tick_counter = 0;

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client_fd, &rfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = tick_ms * 1000;

        /* Wait up to one tick for client input, then continue simulation. */
        int ret = select(client_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0 && errno != EINTR) break;

        /* Emit state only when changed to avoid unnecessary WS traffic. */
        int dirty = 0;
        if (ret > 0 && FD_ISSET(client_fd, &rfds)) {
            int got = ws_read_message(client_fd, &buf, msg, sizeof(msg));
            if (got < 0) break;
            if (got > 0) {
                char action[64];
                if (parse_action(msg, action, sizeof(action))) {
                    handle_action(&game, action);
                    dirty = 1;
                }
            }
        }

        if (!game.paused && !game.game_over) {
            tick_counter++;
            int level = game.level;
            if (level < 1) level = 1;
            /* Higher level => fewer ticks per gravity step => faster fall. */
            drop_ticks = 14 - level;
            if (drop_ticks < 2) drop_ticks = 2;
            if (tick_counter >= drop_ticks) {
                tick_counter = 0;
                step_down(&game);
                dirty = 1;
            }
        }

        if (dirty) {
            /* Push full snapshot; UI is stateless between messages. */
            build_state_json(&game, json, sizeof(json));
            if (ws_send_text(client_fd, json) < 0) break;
        }
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
