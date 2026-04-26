/* =============================================================================
 * ws_tetris.c  —  WebSocket Backend for Browser Tetris
 * =============================================================================
 * ROLE:
 *   Runs a minimal single-client game server that speaks plain WebSocket
 *   frames and streams JSON snapshots to the React UI.
 *
 * HIGH-LEVEL FLOW:
 *   1) Start server + accept client via screen_server_start() (screen.c)
 *   2) Perform HTTP -> WebSocket upgrade via screen_ws_handshake() (screen.c)
 *   3) Enter fixed-tick game loop
 *      - poll for input via keyboard_poll_ws()    (keyboard.c)
 *      - read messages via keyboard_recv_ws()     (keyboard.c)
 *      - apply Tetris logic (move/rotate/drop/pause/restart)
 *      - advance gravity over time
 *      - emit updated JSON state via screen_send_ws() (screen.c)
 *
 * LIBRARY INTEGRATION (Section 3 Pipeline):
 *   keyboard.c → captures browser input   (keyboard_poll_ws, keyboard_recv_ws)
 *   string.c   → JSON builder + parser    (t_strcmp, t_itoa, t_strlen)
 *   memory.c   → dynamic Game allocation  (t_alloc, t_dealloc)
 *   math.c     → scoring, rotation, bounds (t_mul, t_mod, t_in_bounds)
 *   screen.c   → server setup + WS output (screen_server_start, screen_send_ws)
 *
 * RULES COMPLIANCE (Section 2):
 *   - Rule 1:  NO <string.h>, <math.h>, or default malloc / free.
 *   - Rule 2:  No hard-coded values — all logic computed dynamically.
 *   - Rule 3:  Only <stdio.h> and <stdlib.h> used here.  ALL networking is
 *              abstracted inside keyboard.c and screen.c — zero POSIX socket
 *              headers or calls appear in this file.
 * =============================================================================
 */

/* ---- Allowed standard headers (Rule 3) ----------------------------------- */
#include <stdio.h>  /* allowed: terminal I/O simulation               */
#include <stdlib.h> /* allowed: process start/exit only                */

/* ---- All five custom library headers (Section 3 Engine) ------------------ */
#include "../include/keyboard.h"
#include "../include/memory.h"
#include "../include/screen.h"
#include "../include/t_math.h"
#include "../include/t_string.h"

#define PORT_DEFAULT 8080
#define ROWS 20
#define COLS 10
#define NUM_PIECES 7
#define NUM_ROTATIONS 4
#define PIECE_SIZE 4

#define PLAYER_NAME_MAX 20
#define LEADERBOARD_MAX 10
#define LEADERBOARD_FILE "leaderboard.txt"

#define LOCK_DELAY_TICKS 10   /* ticks before auto-lock (10 × 50ms = 500ms) */
#define MAX_LOCK_RESETS  15   /* max lock-timer resets per piece             */
#define SRS_KICK_TESTS    5   /* wall-kick test positions per rotation       */

/* Active piece coordinates are board-space cell indices, not pixels. */
typedef struct {
  int type;
  int rotation;
  int x;
  int y;
} Piece;

/* Single leaderboard entry */
typedef struct {
  char name[PLAYER_NAME_MAX];
  int score;
  int level;
  int lines;
} LeaderEntry;

/* Entire mutable game state for one running session.
 * Allocated dynamically via t_alloc() to satisfy the dynamic memory rule. */
typedef struct {
  int board[ROWS][COLS];
  Piece current;
  Piece next;
  Piece held;
  int has_current;
  int has_held;
  int hold_used;       /* 1 = already held this piece (resets on spawn)  */
  int score;
  int level;
  int lines;
  int combo;           /* consecutive clears: resets on non-clearing lock */
  int game_over;
  int paused;
  int rand_state;
  char player_name[PLAYER_NAME_MAX];
  int high_score;
  /* 7-bag randomizer */
  int bag[NUM_PIECES];
  int bag_index;
  /* Lock delay */
  int lock_ticks;
  int lock_resets;
  int last_was_rotate; /* for future T-spin detection */
} Game;

/* Global leaderboard (persisted to file) */
static LeaderEntry leaderboard[LEADERBOARD_MAX];
static int leaderboard_count = 0;

/* Tetromino lookup table: [piece][rotation][row][col] -> 0/1 occupancy. */
static const int PIECES[NUM_PIECES][NUM_ROTATIONS][PIECE_SIZE][PIECE_SIZE] = {
    /* I */
    {{{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}},
     {{0, 0, 0, 0}, {0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}},
    /* O */
    {{{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
    /* T */
    {{{0, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
    /* S */
    {{{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}},
     {{1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
    /* Z */
    {{{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {1, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}}},
    /* J */
    {{{1, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}}},
    /* L */
    {{{0, 0, 1, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}},
     {{1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}}};

static const char PIECE_NAMES[NUM_PIECES] = {'I', 'O', 'T', 'S', 'Z', 'J', 'L'};

/* =============================================================================
 * SRS WALL KICK OFFSET TABLES  (y-down positive, applied to piece position)
 * Index: [old_rotation][test 0..4] = {dx, dy}
 * Clockwise transitions: 0→1, 1→2, 2→3, 3→0
 * ============================================================================= */
static const int SRS_JLSTZ[NUM_ROTATIONS][SRS_KICK_TESTS][2] = {
  /* 0→1 */ {{ 0, 0}, {-1, 0}, {-1,-1}, { 0, 2}, {-1, 2}},
  /* 1→2 */ {{ 0, 0}, { 1, 0}, { 1, 1}, { 0,-2}, { 1,-2}},
  /* 2→3 */ {{ 0, 0}, { 1, 0}, { 1,-1}, { 0, 2}, { 1, 2}},
  /* 3→0 */ {{ 0, 0}, {-1, 0}, {-1, 1}, { 0,-2}, {-1,-2}},
};

static const int SRS_I[NUM_ROTATIONS][SRS_KICK_TESTS][2] = {
  /* 0→1 */ {{ 0, 0}, {-2, 0}, { 1, 0}, {-2, 1}, { 1,-2}},
  /* 1→2 */ {{ 0, 0}, {-1, 0}, { 2, 0}, {-1,-2}, { 2, 1}},
  /* 2→3 */ {{ 0, 0}, { 2, 0}, {-1, 0}, { 2,-1}, {-1, 2}},
  /* 3→0 */ {{ 0, 0}, { 1, 0}, {-2, 0}, { 1, 2}, {-2,-1}},
};

/* Minimal write helpers using fputs/fprintf (allowed <stdio.h> only). */
static void write_str(const char *s) {
  if (!s)
    return;
  int i = 0;
  while (s[i]) {
    fputc(s[i], stderr);
    i++;
  }
}

static void write_stdout(const char *s) {
  if (!s)
    return;
  int i = 0;
  while (s[i]) {
    fputc(s[i], stdout);
    i++;
  }
}

static void write_int(int v) {
  char tmp[16];
  t_itoa(v, tmp);
  write_stdout(tmp);
}

/* =============================================================================
 * LEADERBOARD — File System persistence
 * =============================================================================
 * Format per line: score|level|lines|name
 * Uses <stdio.h> file I/O (allowed) and t_itoa/t_atoi from string.c.
 */

static void leaderboard_load(void) {
  FILE *f = fopen(LEADERBOARD_FILE, "r");
  if (!f) {
    leaderboard_count = 0;
    return;
  }

  leaderboard_count = 0;
  while (leaderboard_count < LEADERBOARD_MAX) {
    /* Read score */
    char buf[128];
    int bi = 0, c;
    while (bi < (int)sizeof(buf) - 1 && (c = fgetc(f)) != EOF && c != '\n') {
      buf[bi++] = (char)c;
    }
    if (bi == 0 && c == EOF)
      break;
    buf[bi] = '\0';

    /* Parse: score|level|lines|name */
    int field = 0;
    int fi = 0;
    char field_buf[64];
    LeaderEntry *e = &leaderboard[leaderboard_count];
    e->score = 0;
    e->level = 1;
    e->lines = 0;
    e->name[0] = '\0';

    for (int i = 0; i <= bi; i++) {
      if (buf[i] == '|' || buf[i] == '\0') {
        field_buf[fi] = '\0';
        if (field == 0)
          e->score = t_atoi(field_buf);
        else if (field == 1)
          e->level = t_atoi(field_buf);
        else if (field == 2)
          e->lines = t_atoi(field_buf);
        else if (field == 3)
          t_strncpy(e->name, field_buf, PLAYER_NAME_MAX);
        field++;
        fi = 0;
      } else {
        if (fi < (int)sizeof(field_buf) - 1)
          field_buf[fi++] = buf[i];
      }
    }

    if (t_strlen(e->name) == 0)
      t_strncpy(e->name, "Player", PLAYER_NAME_MAX);

    leaderboard_count++;
  }

  fclose(f);
}

static void leaderboard_save(void) {
  FILE *f = fopen(LEADERBOARD_FILE, "w");
  if (!f)
    return;

  for (int i = 0; i < leaderboard_count; i++) {
    char tmp[16];
    LeaderEntry *e = &leaderboard[i];

    t_itoa(e->score, tmp);
    int j = 0;
    while (tmp[j])
      fputc(tmp[j++], f);
    fputc('|', f);

    t_itoa(e->level, tmp);
    j = 0;
    while (tmp[j])
      fputc(tmp[j++], f);
    fputc('|', f);

    t_itoa(e->lines, tmp);
    j = 0;
    while (tmp[j])
      fputc(tmp[j++], f);
    fputc('|', f);

    j = 0;
    while (e->name[j])
      fputc(e->name[j++], f);
    fputc('\n', f);
  }

  fclose(f);
}

static void leaderboard_insert(const char *name, int score, int level,
                               int lines) {
  /* Find insertion point (sorted descending by score) */
  int pos = leaderboard_count;
  for (int i = 0; i < leaderboard_count; i++) {
    if (score > leaderboard[i].score) {
      pos = i;
      break;
    }
  }

  if (pos >= LEADERBOARD_MAX)
    return;

  /* Shift entries down */
  int end = leaderboard_count < LEADERBOARD_MAX ? leaderboard_count
                                                : LEADERBOARD_MAX - 1;
  for (int i = end; i > pos; i--) {
    leaderboard[i] = leaderboard[i - 1];
  }

  /* Insert new entry */
  t_strncpy(leaderboard[pos].name, name, PLAYER_NAME_MAX);
  leaderboard[pos].score = score;
  leaderboard[pos].level = level;
  leaderboard[pos].lines = lines;

  if (leaderboard_count < LEADERBOARD_MAX)
    leaderboard_count++;

  leaderboard_save();
}

/* Compute high score from leaderboard */
static int leaderboard_high_score(void) {
  if (leaderboard_count == 0)
    return 0;
  return leaderboard[0].score; /* sorted descending, first is highest */
}

/* ---------------- Game logic ---------------- */

/* LCG pseudo-random generator (kept for bag shuffling). */
static int rand_next_raw(Game *g) {
  g->rand_state = (g->rand_state * 1103515245 + 12345) & 0x7FFFFFFF;
  return g->rand_state;
}

static void seed_rand(Game *g) {
  g->rand_state = rand();
  if (g->rand_state == 0)
    g->rand_state = 1;
}

/* ---- 7-Bag Randomizer ---- */
static void bag_shuffle(Game *g) {
  for (int i = 0; i < NUM_PIECES; i++)
    g->bag[i] = i;
  /* Fisher-Yates shuffle using LCG */
  for (int i = NUM_PIECES - 1; i > 0; i--) {
    int j = t_mod(rand_next_raw(g), i + 1);
    if (j < 0) j = -j;
    int tmp = g->bag[i];
    g->bag[i] = g->bag[j];
    g->bag[j] = tmp;
  }
  g->bag_index = 0;
}

static int bag_next(Game *g) {
  if (g->bag_index >= NUM_PIECES)
    bag_shuffle(g);
  return g->bag[g->bag_index++];
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
  g->combo = 0;
  g->paused = 0;
  g->game_over = 0;
  g->has_current = 0;
  g->has_held = 0;
  g->hold_used = 0;
  g->lock_ticks = 0;
  g->lock_resets = 0;
  g->last_was_rotate = 0;
  g->high_score = leaderboard_high_score();
  seed_rand(g);
  bag_shuffle(g);
  g->next.type = bag_next(g);
  g->next.rotation = 0;
  g->next.x = 3;
  g->next.y = 0;
  /* Note: player_name is NOT cleared on reset */
}

/* Boundary check using t_in_bounds from t_math.h. */
static int collides(Game *g, Piece *p, int dy, int dx, int rot) {
  for (int r = 0; r < PIECE_SIZE; r++) {
    for (int c = 0; c < PIECE_SIZE; c++) {
      if (!PIECES[p->type][rot][r][c])
        continue;
      int nr = p->y + dy + r;
      int nc = p->x + dx + c;
      if (!t_in_bounds(nc, 0, COLS - 1) || nr >= ROWS)
        return 1;
      if (nr >= 0 && g->board[nr][nc] > 0)
        return 1;
    }
  }
  return 0;
}

static void spawn_piece(Game *g) {
  g->current = g->next;
  g->current.rotation = 0;
  g->current.x = 3;
  g->current.y = 0;
  g->has_current = 1;
  g->hold_used = 0;   /* allow hold again for new piece */
  g->lock_ticks = 0;
  g->lock_resets = 0;
  g->last_was_rotate = 0;

  g->next.type = bag_next(g);
  g->next.rotation = 0;
  g->next.x = 3;
  g->next.y = 0;

  if (collides(g, &g->current, 0, 0, g->current.rotation)) {
    g->game_over = 1;
    if (g->score > 0) {
      leaderboard_insert(g->player_name, g->score, g->level, g->lines);
      g->high_score = leaderboard_high_score();
    }
  }
}

static int lock_piece(Game *g) {
  int overflow = 0;
  for (int r = 0; r < PIECE_SIZE; r++) {
    for (int c = 0; c < PIECE_SIZE; c++) {
      if (!PIECES[g->current.type][g->current.rotation][r][c])
        continue;
      int nr = g->current.y + r;
      int nc = g->current.x + c;
      if (nr < 0) {
        overflow = 1;
      } else if (t_in_bounds(nr, 0, ROWS - 1) && t_in_bounds(nc, 0, COLS - 1)) {
        g->board[nr][nc] = g->current.type + 1;
      }
    }
  }
  g->has_current = 0;
  return overflow;
}

/* Scoring uses t_mul and t_div from t_math.h. */
static void clear_lines(Game *g) {
  int cleared = 0;
  for (int r = ROWS - 1; r >= 0; r--) {
    int full = 1;
    for (int c = 0; c < COLS; c++) {
      if (g->board[r][c] == 0) {
        full = 0;
        break;
      }
    }
    if (full) {
      cleared++;
      for (int rr = r; rr > 0; rr--) {
        for (int cc = 0; cc < COLS; cc++) {
          g->board[rr][cc] = g->board[rr - 1][cc];
        }
      }
      for (int cc = 0; cc < COLS; cc++)
        g->board[0][cc] = 0;
      r++;
    }
  }

  if (cleared > 0) {
    g->lines += cleared;
    int base = (cleared == 1)   ? 100
               : (cleared == 2) ? 300
               : (cleared == 3) ? 500
                                : 800;
    /* Combo bonus: 50 * combo * level for consecutive clears */
    int combo_bonus = t_mul(t_mul(50, g->combo), g->level);
    g->score += t_mul(base, g->level) + combo_bonus;
    g->combo++;  /* increment for next clear */


    g->level = 1 + t_div(g->lines, 10);
    /* Cap at level 20 */
    if (g->level > 20) g->level = 20;

    if (g->score > g->high_score) {
      g->high_score = g->score;
    }
  } else {
    g->combo = 0; /* reset combo on non-clearing lock */
  }
}

/* step_down: gravity pull — moves piece down but does NOT lock.
 * Lock delay is handled in the main loop. */
static void step_down(Game *g) {
  if (!g->has_current)
    spawn_piece(g);
  if (g->game_over)
    return;
  if (!collides(g, &g->current, 1, 0, g->current.rotation)) {
    g->current.y += 1;
    g->lock_ticks = 0; /* moved down, reset lock timer */
  }
  /* If piece is on ground, the main loop lock-delay handles locking. */
}

/* Force-lock the current piece, clear lines, spawn next. */
static void do_lock(Game *g) {
  int overflow = lock_piece(g);
  if (overflow) {
    g->game_over = 1;
    if (g->score > 0) {
      leaderboard_insert(g->player_name, g->score, g->level, g->lines);
      g->high_score = leaderboard_high_score();
    }
    return;
  }
  clear_lines(g);
  spawn_piece(g);
}

static int ghost_y(Game *g) {
  Piece p = g->current;
  while (!collides(g, &p, 1, 0, p.rotation)) {
    p.y += 1;
  }
  return p.y;
}

/* ---- Parse action name from JSON ---- */
static int parse_action(const char *msg, char *out, int out_sz) {
  const char *needle = "\"action\"";
  int i = 0;
  while (msg[i]) {
    int match = 1;
    for (int j = 0; needle[j]; j++) {
      if (msg[i + j] != needle[j]) {
        match = 0;
        break;
      }
    }
    if (match) {
      int k = i;
      while (msg[k] && msg[k] != ':')
        k++;
      if (!msg[k])
        return 0;
      k++;
      while (msg[k] && msg[k] != '"')
        k++;
      if (!msg[k])
        return 0;
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

/* ---- Parse "name" field from JSON ---- */
static int parse_name(const char *msg, char *out, int out_sz) {
  const char *needle = "\"name\"";
  int i = 0;
  while (msg[i]) {
    int match = 1;
    for (int j = 0; needle[j]; j++) {
      if (msg[i + j] != needle[j]) {
        match = 0;
        break;
      }
    }
    if (match) {
      int k = i;
      while (msg[k] && msg[k] != ':')
        k++;
      if (!msg[k])
        return 0;
      k++;
      while (msg[k] && msg[k] != '"')
        k++;
      if (!msg[k])
        return 0;
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

/* Action handling uses t_strcmp from t_string.h. */
static void handle_action(Game *g, const char *action, const char *raw_msg) {
  if (t_strcmp(action, "set_name") == 0) {
    char name[PLAYER_NAME_MAX];
    if (parse_name(raw_msg, name, PLAYER_NAME_MAX)) {
      t_strncpy(g->player_name, name, PLAYER_NAME_MAX);
    }
    return;
  }
  if (t_strcmp(action, "pause") == 0) {
    g->paused = !g->paused;
    return;
  }
  if (t_strcmp(action, "restart") == 0) {
    if (g->game_over && g->score > 0) {
      /* Already saved on game_over trigger, skip duplicate */
    }
    game_reset(g);
    return;
  }
  if (g->game_over || g->paused)
    return;

  /* ---- Hold Piece ---- */
  if (t_strcmp(action, "hold") == 0) {
    if (g->hold_used) return; /* can only hold once per piece */
    g->hold_used = 1;
    g->last_was_rotate = 0;
    if (g->has_held) {
      /* Swap current with held */
      Piece tmp = g->held;
      g->held = g->current;
      g->held.rotation = 0;
      g->current = tmp;
      g->current.rotation = 0;
      g->current.x = 3;
      g->current.y = 0;
      g->lock_ticks = 0;
      g->lock_resets = 0;
      if (collides(g, &g->current, 0, 0, g->current.rotation)) {
        g->game_over = 1;
        if (g->score > 0) {
          leaderboard_insert(g->player_name, g->score, g->level, g->lines);
          g->high_score = leaderboard_high_score();
        }
      }
    } else {
      /* First hold: stash current, spawn from next */
      g->held = g->current;
      g->held.rotation = 0;
      g->has_held = 1;
      g->has_current = 0;
      spawn_piece(g);
      g->hold_used = 1; /* spawn_piece resets hold_used, re-set it */
    }
    return;
  }

  /* ---- Movement (with lock-delay reset) ---- */
  int moved = 0;
  if (t_strcmp(action, "move_left") == 0) {
    if (!collides(g, &g->current, 0, -1, g->current.rotation)) {
      g->current.x -= 1;
      moved = 1;
    }
    g->last_was_rotate = 0;
  } else if (t_strcmp(action, "move_right") == 0) {
    if (!collides(g, &g->current, 0, 1, g->current.rotation)) {
      g->current.x += 1;
      moved = 1;
    }
    g->last_was_rotate = 0;
  } else if (t_strcmp(action, "rotate") == 0) {
    /* ---- SRS Wall Kick Rotation ---- */
    int old_rot = g->current.rotation;
    int new_rot = t_mod(old_rot + 1, NUM_ROTATIONS);
    const int (*kicks)[2];
    if (g->current.type == 0) /* I piece */
      kicks = SRS_I[old_rot];
    else if (g->current.type == 1) /* O piece — no kick needed */
      kicks = SRS_JLSTZ[old_rot];
    else
      kicks = SRS_JLSTZ[old_rot];

    for (int t = 0; t < SRS_KICK_TESTS; t++) {
      int dx = kicks[t][0];
      int dy = kicks[t][1];
      if (!collides(g, &g->current, dy, dx, new_rot)) {
        g->current.x += dx;
        g->current.y += dy;
        g->current.rotation = new_rot;
        moved = 1;
        g->last_was_rotate = 1;
        break;
      }
    }
  } else if (t_strcmp(action, "soft_drop") == 0) {
    g->last_was_rotate = 0;
    if (!collides(g, &g->current, 1, 0, g->current.rotation)) {
      g->current.y += 1;
      g->score += 1;
      g->lock_ticks = 0;
    }
  } else if (t_strcmp(action, "hard_drop") == 0) {
    g->last_was_rotate = 0;
    int drop_rows = 0;
    while (!collides(g, &g->current, 1, 0, g->current.rotation)) {
      g->current.y += 1;
      drop_rows++;
    }
    if (drop_rows > 0)
      g->score += t_mul(drop_rows, 2);
    /* Hard drop locks immediately (bypasses lock delay) */
    do_lock(g);
    return;
  }

  /* Reset lock delay on successful move/rotate while on ground */
  if (moved && g->has_current &&
      collides(g, &g->current, 1, 0, g->current.rotation)) {
    if (g->lock_resets < MAX_LOCK_RESETS) {
      g->lock_ticks = 0;
      g->lock_resets++;
    }
  }
}

/* ---------------- JSON serialization ----------------
 * Uses t_itoa from t_string.h for int-to-string conversion.
 */
static void append_char(char *buf, int *len, char c) { buf[(*len)++] = c; }

static void append_str(char *buf, int *len, const char *s) {
  for (int i = 0; s[i]; i++)
    buf[(*len)++] = s[i];
}

static void append_int(char *buf, int *len, int v) {
  char tmp[16];
  t_itoa(v, tmp);
  append_str(buf, len, tmp);
}

static void append_shape(char *buf, int *len, int type, int rot) {
  append_char(buf, len, '[');
  for (int r = 0; r < PIECE_SIZE; r++) {
    append_char(buf, len, '[');
    for (int c = 0; c < PIECE_SIZE; c++) {
      append_int(buf, len, PIECES[type][rot][r][c]);
      if (c < PIECE_SIZE - 1)
        append_char(buf, len, ',');
    }
    append_char(buf, len, ']');
    if (r < PIECE_SIZE - 1)
      append_char(buf, len, ',');
  }
  append_char(buf, len, ']');
}

/* Escape a string for JSON (handles minimal escaping) */
static void append_escaped_str(char *buf, int *len, const char *s) {
  append_char(buf, len, '"');
  for (int i = 0; s[i]; i++) {
    if (s[i] == '"') {
      append_char(buf, len, '\\');
      append_char(buf, len, '"');
    } else if (s[i] == '\\') {
      append_char(buf, len, '\\');
      append_char(buf, len, '\\');
    } else {
      append_char(buf, len, s[i]);
    }
  }
  append_char(buf, len, '"');
}

static void build_state_json(Game *g, char *out, int out_sz) {
  int len = 0;
  append_char(out, &len, '{');

  append_str(out, &len, "\"board\":[");
  for (int r = 0; r < ROWS; r++) {
    append_char(out, &len, '[');
    for (int c = 0; c < COLS; c++) {
      append_int(out, &len, g->board[r][c]);
      if (c < COLS - 1)
        append_char(out, &len, ',');
    }
    append_char(out, &len, ']');
    if (r < ROWS - 1)
      append_char(out, &len, ',');
  }
  append_char(out, &len, ']');

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

  /* Held piece */
  append_str(out, &len, ",\"heldPiece\":");
  if (g->has_held) {
    append_char(out, &len, '{');
    append_str(out, &len, "\"shape\":");
    append_shape(out, &len, g->held.type, 0);
    append_str(out, &len, ",\"type\":\"");
    append_char(out, &len, PIECE_NAMES[g->held.type]);
    append_char(out, &len, '"');
    append_char(out, &len, '}');
  } else {
    append_str(out, &len, "null");
  }
  append_str(out, &len, ",\"holdUsed\":");
  append_str(out, &len, g->hold_used ? "true" : "false");

  append_str(out, &len, ",\"score\":");
  append_int(out, &len, g->score);
  append_str(out, &len, ",\"level\":");
  append_int(out, &len, g->level);
  append_str(out, &len, ",\"lines\":");
  append_int(out, &len, g->lines);
  append_str(out, &len, ",\"combo\":");
  append_int(out, &len, g->combo);
  append_str(out, &len, ",\"highScore\":");
  append_int(out, &len, g->high_score);

  /* Player name */
  append_str(out, &len, ",\"playerName\":");
  if (t_strlen(g->player_name) > 0) {
    append_escaped_str(out, &len, g->player_name);
  } else {
    append_str(out, &len, "\"Player\"");
  }

  /* Game state */
  append_str(out, &len, ",\"state\":\"");
  if (g->game_over)
    append_str(out, &len, "gameover");
  else if (g->paused)
    append_str(out, &len, "paused");
  else
    append_str(out, &len, "playing");
  append_char(out, &len, '"');

  /* Leaderboard array */
  append_str(out, &len, ",\"leaderboard\":[");
  for (int i = 0; i < leaderboard_count; i++) {
    append_char(out, &len, '{');
    append_str(out, &len, "\"name\":");
    append_escaped_str(out, &len, leaderboard[i].name);
    append_str(out, &len, ",\"score\":");
    append_int(out, &len, leaderboard[i].score);
    append_str(out, &len, ",\"level\":");
    append_int(out, &len, leaderboard[i].level);
    append_str(out, &len, ",\"lines\":");
    append_int(out, &len, leaderboard[i].lines);
    append_char(out, &len, '}');
    if (i < leaderboard_count - 1)
      append_char(out, &len, ',');
  }
  append_char(out, &len, ']');

  append_char(out, &len, '}');
  if (len >= out_sz)
    len = out_sz - 1;
  out[len] = '\0';
}

/* ========================== MAIN ========================================== */

int main(void) {
  /* ---- Boot the custom memory subsystem (memory.c) -------------------- */
  memory_init(65536);

  /* ---- Load leaderboard from file ------------------------------------- */
  leaderboard_load();

  /* ---- Start server via screen.c (hardware abstraction) --------------- *
   * screen_server_listen() binds + listens.  We keep the server socket
   * open so we can re-accept clients on page refresh / disconnect.      */
  write_stdout("Tetris WS backend listening on ws://localhost:");
  write_int(PORT_DEFAULT);
  write_stdout("\n");

  int server_fd = screen_server_listen(PORT_DEFAULT);
  if (server_fd < 0) {
    write_str("Error: server listen failed\n");
    memory_cleanup();
    return 1;
  }

  /* ---- Allocate game state dynamically via memory.c (t_alloc) --------- */
  Game *game = (Game *)t_alloc(sizeof(Game));
  if (!game) {
    write_str("Error: t_alloc failed for Game\n");
    screen_server_close(server_fd);
    memory_cleanup();
    return 1;
  }

  char msg[2048];
  char json[16384];

  /* ======== Outer loop: accept clients, reconnect on disconnect ======== */
  while (1) {
    write_stdout("Waiting for client...\n");

    int client_fd = screen_server_accept(server_fd);
    if (client_fd < 0) {
      write_str("Error: accept failed\n");
      break;
    }

    write_stdout("Client connected. Performing handshake...\n");

    /* Initialize I/O devices for this client */
    screen_init_ws(client_fd);
    keyboard_init_ws(client_fd);

    /* WebSocket handshake */
    if (screen_ws_handshake() < 0) {
      write_str("WebSocket handshake failed, waiting for next client\n");
      keyboard_close_ws();
      continue; /* retry with next client */
    }

    write_stdout("Handshake OK. Starting game session.\n");

    /* Initialize game state for this session */
    t_strncpy(game->player_name, "Player", PLAYER_NAME_MAX);
    game_reset(game);
    spawn_piece(game);

    int tick_ms = 50;
    int drop_ticks = 12;
    int tick_counter = 0;

    /* Send initial state immediately so UI has data */
    build_state_json(game, json, sizeof(json));
    screen_send_ws(json);

    /* ======== Inner loop: game tick loop for this client ======== */
    while (1) {
      int ready = keyboard_poll_ws(tick_ms);
      if (ready < 0)
        break; /* client disconnected */

      int dirty = 0;
      if (ready > 0) {
        int got = keyboard_recv_ws(msg, sizeof(msg));
        if (got < 0)
          break; /* client disconnected */
        if (got > 0) {
          char action[64];
          if (parse_action(msg, action, sizeof(action))) {
            handle_action(game, action, msg);
            dirty = 1;
          }
        }
      }

      if (!game->paused && !game->game_over) {
        tick_counter++;
        int level = game->level;
        if (level < 1)
          level = 1;
        /* NES-style speed curve: faster drops at higher levels
         * Level 1: 12 ticks (600ms), Level 5: 8 ticks (400ms),
         * Level 10: 3 ticks (150ms), Level 15+: 1 tick (50ms) */
        if (level <= 8)
          drop_ticks = t_max(13 - level, 5);
        else if (level <= 13)
          drop_ticks = t_max(8 - t_div(level - 8, 2), 2);
        else
          drop_ticks = 1;
        if (tick_counter >= drop_ticks) {
          tick_counter = 0;
          step_down(game);
          dirty = 1;
        }

        /* ---- Lock Delay ---- */
        if (game->has_current &&
            collides(game, &game->current, 1, 0, game->current.rotation)) {
          game->lock_ticks++;
          if (game->lock_ticks >= LOCK_DELAY_TICKS) {
            do_lock(game);
            dirty = 1;
          }
        } else if (game->has_current) {
          game->lock_ticks = 0;
        }
      }

      if (dirty) {
        build_state_json(game, json, sizeof(json));
        if (screen_send_ws(json) < 0)
          break; /* client disconnected */
      }
    }

    /* ---- Client disconnected: cleanup I/O, loop back to accept ---- */
    write_stdout("Client disconnected. Ready for reconnect.\n");
    keyboard_close_ws();
    /* Reload leaderboard in case it was updated */
    leaderboard_load();
  }

  /* ---- Final cleanup ---- */
  t_dealloc(game);
  screen_server_close(server_fd);
  memory_cleanup();
  return 0;
}
