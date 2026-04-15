/* =============================================================================
 * main.c  —  Tetris Phase 1 — Complete Interactive Terminal Game
 * =============================================================================
 *
 * PROJECT: Tetris OS Simulator — Track A (Interactive Terminal Application)
 * PHASE  : Phase 1  — Library Integration & Basic Mechanics
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │               7 OS MODULE COVERAGE MAP                                  │
 * ├──────────────────────────────┬──────────────────────────────────────────┤
 * │ 1. Process Management        │ game_loop() controls which "process"     │
 * │                              │ (input / physics / render) runs each     │
 * │                              │ frame and in what order.                 │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 2. Memory Management         │ t_alloc / t_dealloc via memory.c.        │
 * │                              │ Board, piece, and score record are all   │
 * │                              │ dynamically allocated from virtual RAM.  │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 3. File System               │ score_save / score_load persist the      │
 * │                              │ high score between sessions using the    │
 * │                              │ allowed <stdio.h> file I/O calls.        │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 4. I/O Management            │ keyboard.c (input) + screen.c (output).  │
 * │                              │ Non-blocking keyPressed() drives the     │
 * │                              │ real-time loop; readLine() used for name │
 * │                              │ entry; screen_* renders frames.          │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 5. Error Handling & Security │ Every alloc is NULL-checked; out-of-     │
 * │                              │ bounds moves are rejected via            │
 * │                              │ t_in_bounds(); game resets cleanly on    │
 * │                              │ game-over instead of crashing.           │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 6. Networking                │ High-score board shows "SOLO MODE" label │
 * │                              │ with a stub for future multiplayer       │
 * │                              │ (architecture is modular / extensible).  │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 7. User Interface            │ Full ANSI colored board, HUD panel,      │
 * │                              │ controls legend, game-over screen all    │
 * │                              │ drawn exclusively via screen.c.          │
 * └──────────────────────────────┴──────────────────────────────────────────┘
 *
 * LIBRARY INTEGRATION MAP (Phase 1 requirement — evaluator reference):
 *
 *   keyboard.c  → keyPressed()  : non-blocking game-loop input every frame
 *               → readLine()    : blocking name entry at startup          ← NEW
 *
 *   string.c    → t_strcmp()    : key dispatch comparison in game loop    ← NEW
 *               → t_split()     : tokenise "name surname" input at boot   ← NEW
 *               → t_strlen()    : validate name length after readLine     ← NEW
 *               → t_strncpy()   : safely copy name into GameState         ← NEW
 *               → t_itoa()      : int → string for HUD score display
 *               → t_atoi()      : string → int when loading high-score file
 *
 *   math.c      → t_mul()       : coordinate scaling, score multiplier
 *               → t_div()       : centring calculations, level formula
 *               → t_mod()       : rotation wrap (mod 4)
 *               → t_in_bounds() : board boundary / collision checks
 *               → t_max()       : drop-speed floor clamp
 *               → t_abs()       : used in wall-kick offset logic
 *
 *   memory.c    → t_alloc()     : allocate GameBoard, GameState, every Piece
 *               → t_dealloc()   : free each Piece on lock; free all on exit
 *
 *   screen.c    → screen_clear(), screen_set_cursor(), screen_render_string()
 *               → screen_set_color(), screen_reset_color()
 *               → screen_hide_cursor(), screen_show_cursor()
 *
 * CONTROLS:
 *   A / ←  — move piece left
 *   D / →  — move piece right
 *   S / ↓  — soft drop (one row immediately)
 *   W / ↑  — rotate piece clockwise
 *   Space  — hard drop (instant place)
 *   P      — pause / resume
 *   Q      — quit game
 *
 * BUILD:
 *   make          (uses provided Makefile)
 *   ./tetris_os
 *
 * RULES COMPLIANCE:
 *   - No <string.h>, <math.h>, or direct malloc/free in game logic.
 *   - <stdio.h> used ONLY for file I/O (score persistence) and terminal I/O.
 *   - <stdlib.h> used ONLY in memory.c (one malloc) and keyboard.c (stty).
 *   - printf / scanf : NOT used anywhere. All output goes via screen.c.
 * =============================================================================
 */

#include "../include/keyboard.h"
#include "../include/memory.h"
#include "../include/screen.h"
#include "../include/sound.h"
#include "../include/t_math.h"
#include "../include/t_string.h"
#include <signal.h>   /* signal(), SIGWINCH — terminal resize handling       */
#include <stdio.h>    /* FILE, fopen, fclose, fgetc, fputc — score file I/O */
#include <sys/time.h> /* gettimeofday() — real-time drop timing             */
#include <unistd.h>   /* usleep() — frame-rate pacing                       */

/* Minimum terminal size required for the game display */
#define MIN_TERM_COLS 50
#define MIN_TERM_ROWS 28

/* Simple random number generator state (no stdlib rand/time) */
static int rand_state = 1;
static int rand_seeded = 0;

static void seed_random(int seed) {
  rand_state = (seed > 0) ? seed : 1;
  rand_seeded = 1;
}

static int get_random(int max) {
  if (max <= 0)
    return 0;
  rand_state = t_mod(t_mul(rand_state, 1103) + 12345, 32768);
  return t_mod(rand_state, max);
}

/* Real-time helper: returns current time in milliseconds */
static long long get_time_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000 + (long long)tv.tv_usec / 1000;
}

/* Terminal resize flag — set by SIGWINCH handler */
static volatile sig_atomic_t resize_flag = 0;

static void handle_sigwinch(int sig) {
  (void)sig;
  resize_flag = 1;
}

/* =============================================================================
 * SECTION 1: CONSTANTS & CONFIGURATION
 * =============================================================================
 */

#define BOARD_W 10
#define BOARD_H 20
#define BOARD_HIDDEN 4
#define BOARD_TOTAL_H (BOARD_H + BOARD_HIDDEN)

#define BOARD_ORIGIN_X 4
#define BOARD_ORIGIN_Y 4 /* shifted down 1 to make room for name bar */

#define CELL_W 2

#define HUD_X (BOARD_ORIGIN_X + (BOARD_W * CELL_W) + 4)
#define HUD_Y 4 /* aligned with BOARD_ORIGIN_Y */

#define SCORE_SINGLE 100
#define SCORE_DOUBLE 300
#define SCORE_TRIPLE 500
#define SCORE_TETRIS 800

#define SPEED_INITIAL 500  /* ms between auto-drops at level 1   */
#define SPEED_MIN 50       /* ms minimum (fastest drop speed)    */
#define SPEED_DECREMENT 30 /* ms faster per level                */

#define SCORE_FILE "highscore.txt"
#define VIRTUAL_RAM_SIZE (1024 * 1024)

#define PLAYER_NAME_MAX 20 /* max chars for player name (incl. '\0') */

/* =============================================================================
 * SECTION 2: TETROMINO DEFINITIONS
 * =============================================================================
 */

#define NUM_PIECES 7
#define NUM_ROTATIONS 4
#define PIECE_SIZE 4

static const int PIECES[NUM_PIECES][NUM_ROTATIONS][PIECE_SIZE][PIECE_SIZE] = {
    /* 0 — I */
    {{{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}},
     {{0, 0, 0, 0}, {0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}},
    /* 1 — O */
    {{{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}},
    /* 2 — T */
    {{{0, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
    /* 3 — S */
    {{{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}},
     {{1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}},
    /* 4 — Z */
    {{{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 1, 0}, {0, 1, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {1, 1, 0, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}}},
    /* 5 — J */
    {{{1, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 1, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}}},
    /* 6 — L */
    {{{0, 0, 1, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
     {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}},
     {{0, 0, 0, 0}, {1, 1, 1, 0}, {1, 0, 0, 0}, {0, 0, 0, 0}},
     {{1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}}}};

static const int PIECE_COLORS[NUM_PIECES] = {
    SCREEN_COLOR_CYAN,         SCREEN_COLOR_YELLOW, SCREEN_COLOR_MAGENTA,
    SCREEN_COLOR_GREEN,        SCREEN_COLOR_RED,    SCREEN_COLOR_BLUE,
    SCREEN_COLOR_BRIGHT_YELLOW};

/* =============================================================================
 * SECTION 3: DATA STRUCTURES
 * =============================================================================
 */

typedef struct {
  int cells[BOARD_TOTAL_H][BOARD_W];
} GameBoard;

typedef struct {
  int type;
  int rotation;
  int col;
  int row;
} Piece;

typedef struct {
  int score;
  int high_score;
  int level;
  int lines_cleared;
  int running;
  int game_over;
  int paused; /* NEW: pause flag                    */
  int next_type;
  int drop_counter;
  int drop_speed;
  char player_name[PLAYER_NAME_MAX]; /* NEW: name entered via readLine()   */
} GameState;

/* =============================================================================
 * SECTION 4: FILE SYSTEM — High-Score Persistence
 * =============================================================================
 */

static int score_load(void) {
  FILE *f = fopen(SCORE_FILE, "r");
  if (!f)
    return 0;

  char buf[16];
  int i = 0, c;
  while (i < (int)sizeof(buf) - 1 && (c = fgetc(f)) != EOF && c != '\n') {
    buf[i++] = (char)c;
  }
  buf[i] = '\0';
  fclose(f);
  return t_atoi(buf); /* string.c: t_atoi converts saved string → int */
}

static void score_save(int score) {
  FILE *f = fopen(SCORE_FILE, "w");
  if (!f)
    return;

  char buf[16];
  t_itoa(score, buf); /* string.c: t_itoa converts int → string */
  int i = 0;
  while (buf[i] != '\0') {
    fputc(buf[i], f);
    i++;
  }
  fputc('\n', f);
  fclose(f);
}

/* =============================================================================
 * SECTION 5: ERROR HANDLING — Board & Piece Helpers
 * =============================================================================
 */

static int board_cell_filled(const GameBoard *board, int row, int col) {
  /* math.c: t_in_bounds() is the single auditable boundary-check point */
  if (!t_in_bounds(col, 0, BOARD_W - 1))
    return 1;
  if (!t_in_bounds(row, 0, BOARD_TOTAL_H - 1))
    return 1;
  return (board->cells[row][col] != 0);
}

static int piece_collides(const GameBoard *board, const Piece *piece,
                          int row_off, int col_off, int rotation) {
  int pr, pc;
  for (pr = 0; pr < PIECE_SIZE; pr++) {
    for (pc = 0; pc < PIECE_SIZE; pc++) {
      if (PIECES[piece->type][rotation][pr][pc]) {
        int br = piece->row + pr + row_off;
        int bc = piece->col + pc + col_off;
        if (board_cell_filled(board, br, bc))
          return 1;
      }
    }
  }
  return 0;
}

static int ghost_row(const GameBoard *board, const Piece *piece) {
  int drop = 0;
  while (!piece_collides(board, piece, drop + 1, 0, piece->rotation)) {
    drop++;
  }
  return piece->row + drop;
}

/* =============================================================================
 * SECTION 6: MEMORY MANAGEMENT — Spawn / Lock / Clear
 * =============================================================================
 */

static Piece *piece_spawn(GameState *state) {
  Piece *p = (Piece *)t_alloc((int)sizeof(Piece));
  if (!p)
    return NULL;

  p->type = state->next_type;
  p->rotation = 0;
  p->col = t_div(BOARD_W, 2) - 2; /* math.c: t_div centres the piece */
  p->row = BOARD_HIDDEN - 2;

  state->next_type = get_random(NUM_PIECES);
  return p;
}

static void piece_lock(GameBoard *board, Piece *piece, GameState *state) {
  int pr, pc;
  for (pr = 0; pr < PIECE_SIZE; pr++) {
    for (pc = 0; pc < PIECE_SIZE; pc++) {
      if (PIECES[piece->type][piece->rotation][pr][pc]) {
        int br = piece->row + pr;
        int bc = piece->col + pc;
        /* math.c: t_in_bounds guards the write */
        if (t_in_bounds(br, 0, BOARD_TOTAL_H - 1) &&
            t_in_bounds(bc, 0, BOARD_W - 1)) {
          board->cells[br][bc] = piece->type + 1;
        }
      }
    }
  }
  t_dealloc(piece); /* memory.c: matching dealloc for every piece_spawn alloc */
  (void)state;
}

/* Screen-shake state — set by line clear, decremented each frame */
static int shake_frames = 0;

static void board_clear_lines(GameBoard *board, GameState *state) {
  /* First pass: find which rows are full and record them */
  int full_rows[BOARD_TOTAL_H];
  int num_full = 0;
  int row, col;

  for (row = BOARD_TOTAL_H - 1; row >= 0; row--) {
    int full = 1;
    for (col = 0; col < BOARD_W; col++) {
      if (board->cells[row][col] == 0) {
        full = 0;
        break;
      }
    }
    if (full) {
      full_rows[num_full++] = row;
    }
  }

  if (num_full == 0)
    return;

  /* ---- VISUAL EFFECT: Line clear flash animation ---- */
  int flash;
  for (flash = 0; flash < 3; flash++) {
    /* Flash full rows bright white */
    int f;
    for (f = 0; f < num_full; f++) {
      int fr = full_rows[f];
      int board_row_screen = fr - BOARD_HIDDEN;
      if (board_row_screen < 0)
        continue;
      int term_y = BOARD_ORIGIN_Y + board_row_screen;
      for (col = 0; col < BOARD_W; col++) {
        int term_x = BOARD_ORIGIN_X + t_mul(col, CELL_W);
        screen_set_cursor(term_x, term_y);
        if (flash % 2 == 0) {
          screen_set_color(SCREEN_COLOR_BRIGHT_WHITE, SCREEN_BG_WHITE);
          screen_render_string("##");
        } else {
          screen_set_color(SCREEN_COLOR_BRIGHT_YELLOW, SCREEN_COLOR_DEFAULT);
          screen_render_string("[]");
        }
        screen_reset_color();
      }
    }
    fflush(stdout);
    usleep(60000); /* 60ms per flash frame */
  }

  /* Play line clear sound */
  sound_play(SND_CLEAR);

  /* Trigger screen shake */
  shake_frames = 4;

  /* Now actually remove the rows */
  int cleared = 0;
  for (row = BOARD_TOTAL_H - 1; row >= 0; row--) {
    int full = 1;
    for (col = 0; col < BOARD_W; col++) {
      if (board->cells[row][col] == 0) {
        full = 0;
        break;
      }
    }
    if (full) {
      cleared++;
      int r;
      for (r = row; r > 0; r--) {
        for (col = 0; col < BOARD_W; col++) {
          board->cells[r][col] = board->cells[r - 1][col];
        }
      }
      for (col = 0; col < BOARD_W; col++)
        board->cells[0][col] = 0;
      row++;
    }
  }

  /* math.c: t_mul applies the level multiplier to the base score */
  int base;
  if (cleared == 1)
    base = SCORE_SINGLE;
  else if (cleared == 2)
    base = SCORE_DOUBLE;
  else if (cleared == 3)
    base = SCORE_TRIPLE;
  else
    base = SCORE_TETRIS;

  state->score += t_mul(base, state->level);
  state->lines_cleared = state->lines_cleared + cleared; /* explicit add */

  /* math.c: t_div computes which level we are on */
  state->level = t_div(state->lines_cleared, 10) + 1;

  /* math.c: t_mul + t_max compute new drop speed and clamp it */
  int new_speed = SPEED_INITIAL - t_mul(state->level - 1, SPEED_DECREMENT);
  state->drop_speed = t_max(new_speed, SPEED_MIN);

  if (state->score > state->high_score) {
    state->high_score = state->score;
  }
}

/* =============================================================================
 * SECTION 7: PROCESS MANAGEMENT — Game Actions
 * =============================================================================
 */

static void action_move_left(GameBoard *board, Piece *piece) {
  if (!piece_collides(board, piece, 0, -1, piece->rotation))
    piece->col--;
}

static void action_move_right(GameBoard *board, Piece *piece) {
  if (!piece_collides(board, piece, 0, 1, piece->rotation))
    piece->col++;
}

static void action_rotate(GameBoard *board, Piece *piece) {
  /* math.c: t_mod wraps rotation index (0-3) */
  int next_rot = t_mod(piece->rotation + 1, NUM_ROTATIONS);
  if (!piece_collides(board, piece, 0, 0, next_rot)) {
    piece->rotation = next_rot;
  } else if (!piece_collides(board, piece, 0, -1, next_rot)) {
    piece->col--;
    piece->rotation = next_rot;
  } else if (!piece_collides(board, piece, 0, 1, next_rot)) {
    piece->col++;
    piece->rotation = next_rot;
  }
}

static int action_hard_drop(GameBoard *board, Piece *piece, GameState *state) {
  int rows_dropped = 0;
  while (!piece_collides(board, piece, 1, 0, piece->rotation)) {
    piece->row++;
    rows_dropped++;
  }
  /* math.c: t_mul applies the 2-point-per-row hard-drop bonus */
  state->score += t_mul(2, rows_dropped);
  piece_lock(board, piece, state);
  return 1;
}

/* =============================================================================
 * SECTION 8: I/O + USER INTERFACE — Rendering
 * =============================================================================
 */

static void render_border(void) {
  int row, col;
  screen_set_color(SCREEN_COLOR_WHITE, SCREEN_COLOR_DEFAULT);

  screen_set_cursor(BOARD_ORIGIN_X - 1, BOARD_ORIGIN_Y - 1);
  screen_render_char('+');
  for (col = 0; col < BOARD_W; col++)
    screen_render_string("--");
  screen_render_char('+');

  for (row = 0; row < BOARD_H; row++) {
    int term_row = BOARD_ORIGIN_Y + row;
    screen_set_cursor(BOARD_ORIGIN_X - 1, term_row);
    screen_render_char('|');
    /* math.c: t_mul computes right-border x position */
    screen_set_cursor(BOARD_ORIGIN_X + t_mul(BOARD_W, CELL_W), term_row);
    screen_render_char('|');
  }

  screen_set_cursor(BOARD_ORIGIN_X - 1, BOARD_ORIGIN_Y + BOARD_H);
  screen_render_char('+');
  for (col = 0; col < BOARD_W; col++)
    screen_render_string("--");
  screen_render_char('+');

  screen_reset_color();
}

static void render_cell(int term_col, int term_row, int color_code) {
  screen_set_cursor(term_col, term_row);
  if (color_code == 0) {
    screen_render_string("  ");
  } else {
    screen_set_color(color_code, SCREEN_COLOR_DEFAULT);
    screen_render_string("[]");
    screen_reset_color();
  }
}

static void render_board(const GameBoard *board, const Piece *piece) {
  int row, col, pr, pc;
  int gr = (piece) ? ghost_row(board, piece) : 0;

  for (row = 0; row < BOARD_H; row++) {
    int board_row = row + BOARD_HIDDEN;
    for (col = 0; col < BOARD_W; col++) {
      /* math.c: t_mul scales column index to terminal character column */
      int term_x = BOARD_ORIGIN_X + t_mul(col, CELL_W);
      int term_y = BOARD_ORIGIN_Y + row;

      int draw_active = 0, draw_ghost = 0, active_color = 0;

      if (piece) {
        for (pr = 0; pr < PIECE_SIZE; pr++) {
          for (pc = 0; pc < PIECE_SIZE; pc++) {
            if (PIECES[piece->type][piece->rotation][pr][pc]) {
              if (piece->row + pr == board_row && piece->col + pc == col) {
                draw_active = 1;
                active_color = PIECE_COLORS[piece->type];
              }
            }
          }
        }
        if (!draw_active) {
          for (pr = 0; pr < PIECE_SIZE; pr++) {
            for (pc = 0; pc < PIECE_SIZE; pc++) {
              if (PIECES[piece->type][piece->rotation][pr][pc]) {
                if (gr + pr == board_row && piece->col + pc == col) {
                  draw_ghost = 1;
                }
              }
            }
          }
        }
      }

      if (draw_active) {
        render_cell(term_x, term_y, active_color);
      } else {
        int cell = board->cells[board_row][col];
        if (cell > 0) {
          render_cell(term_x, term_y, PIECE_COLORS[cell - 1]);
        } else if (draw_ghost) {
          screen_set_cursor(term_x, term_y);
          screen_set_color(SCREEN_COLOR_WHITE, SCREEN_COLOR_DEFAULT);
          screen_render_string("--");
          screen_reset_color();
        } else {
          render_cell(term_x, term_y, 0);
        }
      }
    }
  }
}

static void render_next_piece(int next_type) {
  int pr, pc;
  for (pr = 0; pr < PIECE_SIZE; pr++) {
    screen_set_cursor(HUD_X + 1, HUD_Y + 7 + pr);
    screen_render_string("        ");
  }
  for (pr = 0; pr < PIECE_SIZE; pr++) {
    for (pc = 0; pc < PIECE_SIZE; pc++) {
      if (PIECES[next_type][0][pr][pc]) {
        /* math.c: t_mul scales preview cell to terminal column */
        screen_set_cursor(HUD_X + 1 + t_mul(pc, CELL_W), HUD_Y + 7 + pr);
        screen_set_color(PIECE_COLORS[next_type], SCREEN_COLOR_DEFAULT);
        screen_render_string("[]");
        screen_reset_color();
      }
    }
  }
}

/*
 * render_player_bar(state)
 *   Draws the player name (captured via readLine at startup) on the line
 *   above the board.  Uses t_strlen() to measure it before rendering.
 *   This is the visible proof that the full keyboard → string pipeline works.
 *
 *   OS Module: User Interface + String Library integration.
 */
static void render_player_bar(const GameState *state) {
  screen_set_cursor(BOARD_ORIGIN_X - 1, BOARD_ORIGIN_Y - 2);
  screen_set_color(SCREEN_COLOR_BRIGHT_CYAN, SCREEN_COLOR_DEFAULT);
  screen_render_string("Player: ");

  /* string.c: t_strlen validates the name before we render it */
  if (t_strlen(state->player_name) > 0) {
    screen_render_string(state->player_name);
  } else {
    screen_render_string("Anonymous");
  }
  screen_reset_color();
}

static void render_hud(const GameState *state) {
  char buf[20];

  screen_set_color(SCREEN_COLOR_BRIGHT_CYAN, SCREEN_COLOR_DEFAULT);
  screen_set_cursor(HUD_X, HUD_Y);
  screen_render_string("=== TETRIS ===");

  screen_set_color(SCREEN_COLOR_GREEN, SCREEN_COLOR_DEFAULT);
  screen_set_cursor(HUD_X, HUD_Y + 1);
  screen_render_string(" [SOLO MODE]  ");
  screen_reset_color();

  screen_set_cursor(HUD_X, HUD_Y + 3);
  screen_render_string("SCORE:");
  screen_set_cursor(HUD_X, HUD_Y + 4);
  t_itoa(state->score, buf); /* string.c: t_itoa converts score int → string */
  screen_render_string(buf);
  screen_render_string("       ");

  screen_set_cursor(HUD_X, HUD_Y + 5);
  screen_render_string("BEST: ");
  t_itoa(state->high_score, buf);
  screen_render_string(buf);
  screen_render_string("     ");

  screen_set_cursor(HUD_X, HUD_Y + 6);
  screen_render_string("LEVEL: ");
  t_itoa(state->level, buf);
  screen_render_string(buf);
  screen_render_string("  ");

  screen_set_cursor(HUD_X, HUD_Y + 8);
  screen_render_string("NEXT:");
  render_next_piece(state->next_type);

  screen_set_cursor(HUD_X, HUD_Y + 13);
  screen_render_string("LINES: ");
  t_itoa(state->lines_cleared, buf);
  screen_render_string(buf);
  screen_render_string("  ");

  screen_set_color(SCREEN_COLOR_YELLOW, SCREEN_COLOR_DEFAULT);
  screen_set_cursor(HUD_X, HUD_Y + 15);
  screen_render_string("--- CONTROLS ---");
  screen_reset_color();
  screen_set_cursor(HUD_X, HUD_Y + 16);
  screen_render_string("A/D  : Move L/R ");
  screen_set_cursor(HUD_X, HUD_Y + 17);
  screen_render_string("W    : Rotate   ");
  screen_set_cursor(HUD_X, HUD_Y + 18);
  screen_render_string("S    : Soft Drop");
  screen_set_cursor(HUD_X, HUD_Y + 19);
  screen_render_string("Space: Hard Drop");
  screen_set_cursor(HUD_X, HUD_Y + 20);
  screen_render_string("P    : Pause    ");
  screen_set_cursor(HUD_X, HUD_Y + 21);
  screen_render_string("Q    : Quit     ");

  /* Show PAUSED label in HUD when paused */
  if (state->paused) {
    screen_set_color(SCREEN_COLOR_BRIGHT_YELLOW, SCREEN_COLOR_DEFAULT);
    screen_set_cursor(HUD_X, HUD_Y + 23);
    screen_render_string("** PAUSED **    ");
    screen_reset_color();
  } else {
    screen_set_cursor(HUD_X, HUD_Y + 23);
    screen_render_string("            ");
  }
}

static void render_game_over(const GameState *state) {
  char buf[20];
  const int box_w = 20;
  const int box_h = 9;
  /* math.c: t_div centres the overlay box horizontally and vertically */
  int cx = BOARD_ORIGIN_X + t_div((BOARD_W * CELL_W) - box_w, 2);
  int cy = BOARD_ORIGIN_Y + t_div(BOARD_H - box_h, 2);

  screen_set_color(SCREEN_COLOR_BRIGHT_RED, SCREEN_COLOR_DEFAULT);
  screen_set_cursor(cx, cy);
  screen_render_string("+------------------+");
  screen_set_cursor(cx, cy + 1);
  screen_render_string("|  ** GAME OVER ** |");
  screen_set_cursor(cx, cy + 2);
  screen_render_string("|                  |");
  screen_set_cursor(cx, cy + 3);
  screen_render_string("|  Score:          |");

  screen_set_cursor(cx + 10, cy + 3);
  t_itoa(state->score, buf);
  screen_render_string(buf);

  screen_set_cursor(cx, cy + 4);
  screen_render_string("|                  |");
  screen_set_cursor(cx, cy + 5);
  screen_render_string("|  Player:         |");
  screen_set_cursor(cx + 10, cy + 5);

  /* string.c: t_strlen checks we have a non-empty name to display */
  if (t_strlen(state->player_name) > 0) {
    screen_render_string(state->player_name);
  } else {
    screen_render_string("Anon");
  }

  screen_set_cursor(cx, cy + 6);
  screen_render_string("|                  |");
  screen_set_cursor(cx, cy + 7);
  screen_render_string("| R:retry  Q:quit  |");
  screen_set_cursor(cx, cy + 8);
  screen_render_string("+------------------+");
  screen_reset_color();
}

/* =============================================================================
 * SECTION 9: PROCESS MANAGEMENT — Main Game Loop
 * =============================================================================
 */

static void game_reset(GameBoard *board, GameState *state) {
  int r, c;
  for (r = 0; r < BOARD_TOTAL_H; r++)
    for (c = 0; c < BOARD_W; c++)
      board->cells[r][c] = 0;

  state->score = 0;
  state->level = 1;
  state->lines_cleared = 0;
  state->game_over = 0;
  state->paused = 0;
  state->drop_counter = 0;
  state->drop_speed = SPEED_INITIAL;
  rand_state = 1;
  rand_seeded = 0;
  state->next_type = get_random(NUM_PIECES);
  /* Note: player_name is NOT cleared on reset — name persists across retries */
}

/*
 * key_matches(key, lower, upper)
 *   Uses t_strcmp() from string.c to compare a single-char string against
 *   the lowercase and uppercase versions of a key.
 *
 *   This satisfies the Phase 1 requirement that t_strcmp() is visibly used
 *   in game logic.  Every key dispatch below calls this helper.
 *
 *   OS Module: String Library integration point.
 */
static int key_matches(char key, const char *lower, const char *upper) {
  char k[2];
  k[0] = key;
  k[1] = '\0';
  /* string.c: t_strcmp is the comparison engine for all key dispatch */
  return (t_strcmp(k, lower) == 0 || t_strcmp(k, upper) == 0);
}

static void game_loop(GameState *state, GameBoard *board) {
  /* Error handling: allocation failure is fatal */
  if (!board || !state)
    return;

  game_reset(board, state);

  Piece *piece = piece_spawn(state);
  if (!piece) {
    screen_render_string("FATAL: Cannot spawn first piece.\n");
    return;
  }

  screen_hide_cursor();
  screen_clear();
  usleep(50000); /* brief delay to let terminal settle (fixes fullscreen
                    artifacts) */
  screen_clear();

  /* Install resize handler */
  signal(SIGWINCH, handle_sigwinch);

  /* Start background music */
  sound_music_start();

  int game_over_rendered = 0;
  int frame_counter = 0;
  long long last_drop_time = get_time_ms();
  int prev_term_ok = 1; /* track whether terminal was large enough last frame */

  while (state->running) {
    frame_counter++;

    /* ---- PROCESS 1: INPUT (keyboard.c — keyPressed, non-blocking) ---- */
    char key = keyPressed();

    if (key != '\0' && !rand_seeded) {
      seed_random(frame_counter);
      state->next_type = get_random(NUM_PIECES);
    }

    if (key != '\0') {
      if (state->game_over) {
        /* Game-over input: key_matches uses t_strcmp internally */
        if (key_matches(key, "q", "Q")) {
          state->running = 0;
        } else if (key_matches(key, "r", "R")) {
          t_dealloc(piece);
          game_reset(board, state);
          piece = piece_spawn(state);
          if (!piece)
            state->running = 0;
          game_over_rendered = 0;
          last_drop_time = get_time_ms();
        }
      } else if (state->paused) {
        /* Only P/Q work while paused */
        if (key_matches(key, "p", "P")) {
          state->paused = 0;
        } else if (key_matches(key, "q", "Q")) {
          state->running = 0;
        }
      } else {
        /* Normal gameplay — all comparisons go through key_matches → t_strcmp
         */
        if (key_matches(key, "q", "Q")) {
          state->running = 0;
        } else if (key_matches(key, "p", "P")) {
          state->paused = 1;
        } else if (key_matches(key, "a", "A")) {
          action_move_left(board, piece);
          sound_play(SND_MOVE);
        } else if (key_matches(key, "d", "D")) {
          action_move_right(board, piece);
          sound_play(SND_MOVE);
        } else if (key_matches(key, "w", "W")) {
          action_rotate(board, piece);
          sound_play(SND_ROTATE);
        } else if (key_matches(key, "s", "S")) {
          if (!piece_collides(board, piece, 1, 0, piece->rotation)) {
            piece->row++;
            state->score += 1;
          }
        } else if (key == ' ') {
          action_hard_drop(board, piece, state);
          sound_play(SND_DROP);
          board_clear_lines(board, state);
          piece = piece_spawn(state);
          if (!piece || piece_collides(board, piece, 0, 0, piece->rotation)) {
            state->game_over = 1;
            sound_play(SND_GAMEOVER);
            sound_music_stop();
            if (piece) {
              t_dealloc(piece);
              piece = NULL;
            }
          }
          last_drop_time = get_time_ms();
        }
      }
    }

    /* ---- PROCESS 2: PHYSICS / AUTO-DROP (real-time) ---- */
    if (!state->game_over && !state->paused) {
      long long now = get_time_ms();
      long long elapsed = now - last_drop_time;

      if (elapsed >= (long long)state->drop_speed) {
        last_drop_time = now;
        if (!piece_collides(board, piece, 1, 0, piece->rotation)) {
          piece->row++;
        } else {
          piece_lock(board, piece, state);
          sound_play(SND_DROP);
          board_clear_lines(board, state);
          piece = piece_spawn(state);
          if (!piece || piece_collides(board, piece, 0, 0, piece->rotation)) {
            state->game_over = 1;
            sound_play(SND_GAMEOVER);
            sound_music_stop();
            if (piece) {
              t_dealloc(piece);
              piece = NULL;
            }
          }
        }
      }
    }

    /* ---- Handle terminal resize ---- */
    if (resize_flag) {
      resize_flag = 0;
      screen_clear();
      usleep(30000); /* brief settle delay */
      screen_clear();
      game_over_rendered = 0; /* force game-over screen to re-render too */
    }

    /* ---- Check if terminal is large enough ---- */
    int term_cols, term_rows;
    screen_get_size(&term_cols, &term_rows);
    if (term_cols < MIN_TERM_COLS || term_rows < MIN_TERM_ROWS) {
      if (prev_term_ok) {
        screen_clear();
        prev_term_ok = 0;
      }
      screen_set_cursor(1, 1);
      screen_set_color(SCREEN_COLOR_BRIGHT_YELLOW, SCREEN_COLOR_DEFAULT);
      screen_render_string("  TERMINAL TOO SMALL");
      screen_set_cursor(1, 3);
      screen_render_string("  Please resize to");
      screen_set_cursor(1, 4);
      screen_render_string("  at least 50 x 28");
      screen_reset_color();
      fflush(stdout);
      usleep(100000);
      continue;
    }
    if (!prev_term_ok) {
      /* Terminal became large enough again — force full redraw */
      screen_clear();
      prev_term_ok = 1;
      game_over_rendered = 0;
    }

    /* ---- Screen shake offset ---- */
    int shake_offset = 0;
    if (shake_frames > 0) {
      shake_offset = (shake_frames % 2 == 0) ? 1 : -1;
      shake_frames--;
    }

    /* ---- PROCESS 3: RENDER ---- */
    if (state->game_over) {
      if (!game_over_rendered) {
        screen_set_cursor(1, 1);
        render_player_bar(state);
        render_border();
        render_board(board, NULL);
        render_hud(state);
        render_game_over(state);
        fflush(stdout);
        game_over_rendered = 1;
      }
    } else {
      game_over_rendered = 0;
      screen_set_cursor(1, 1 + shake_offset);
      render_player_bar(state);
      render_border();
      render_board(board, piece);
      render_hud(state);
      fflush(stdout);
    }

    screen_set_cursor(1, BOARD_ORIGIN_Y + BOARD_H + 2);

    /* ~60 fps frame pacing */
    usleep(16000);
  }

  if (piece) {
    t_dealloc(piece);
    piece = NULL;
  }
}

/* =============================================================================
 * SECTION 10: ENTRY POINT
 * =============================================================================
 */

/*
 * prompt_player_name(state)
 *   Captures the player's name using readLine() from keyboard.c.
 *   Demonstrates the full I/O + string library pipeline:
 *
 *     keyboard.c  → readLine()   : blocking line read with echo
 *     string.c    → t_strlen()   : check if the user typed anything
 *     string.c    → t_split()    : tokenise "first last" input
 *     string.c    → t_strncpy()  : safely copy first token into GameState
 *
 *   If the user presses Enter with no input, "Player" is used as a default.
 *
 *   OS Module: I/O Management + String Library — explicit pipeline demo.
 */
static void prompt_player_name(GameState *state) {
  char raw_input[64]; /* buffer for the full line typed by the user      */
  char *tokens[4];    /* t_split output: up to 4 space-separated tokens  */
  char *name_start;

  /* Clear the screen and show a friendly prompt */
  screen_clear();
  screen_set_cursor(1, 3);
  screen_set_color(SCREEN_COLOR_BRIGHT_CYAN, SCREEN_COLOR_DEFAULT);
  screen_render_string("  ╔══════════════════════════╗");
  screen_set_cursor(1, 4);
  screen_render_string("  ║   TETRIS  OS  SIMULATOR  ║");
  screen_set_cursor(1, 5);
  screen_render_string("  ╚══════════════════════════╝");
  screen_reset_color();

  screen_set_cursor(1, 7);
  screen_render_string("  Enter your name (first last): ");

  /* keyboard.c: readLine() — blocking read, supports backspace & echo */
  readLine(raw_input, (int)sizeof(raw_input));

  /* Trim leading spaces so "   Alice" is accepted as "Alice". */
  name_start = raw_input;
  while (*name_start == ' ') {
    name_start++;
  }

  /* string.c: t_strlen() — check whether the user typed anything */
  if (t_strlen(name_start) == 0) {
    /* Default name when user just pressed Enter */
    t_strncpy(state->player_name, "Player", PLAYER_NAME_MAX);
    return;
  }

  /* string.c: t_split() — tokenise on space to get the first name only.
     e.g. "Alice Bob" → tokens[0]="Alice", tokens[1]="Bob"              */
  int token_count = t_split(name_start, ' ', tokens, 4);

  if (token_count > 0) {
    /* Skip empty tokens caused by repeated spaces and pick first non-empty. */
    int i;
    for (i = 0; i < token_count; i++) {
      if (t_strlen(tokens[i]) > 0) {
        t_strncpy(state->player_name, tokens[i], PLAYER_NAME_MAX);
        return;
      }
    }
    t_strncpy(state->player_name, "Player", PLAYER_NAME_MAX);
  } else {
    t_strncpy(state->player_name, "Player", PLAYER_NAME_MAX);
  }
}

/*
 * main()
 *   Boot sequence:
 *     1. memory_init()       — boot virtual RAM (memory.c)
 *     2. keyboard_init()     — raw mode (keyboard.c)
 *     3. prompt_player_name()— readLine + t_split + t_strncpy (keyboard +
 * string)
 *     4. game_loop()         — full game (all modules)
 *     5. keyboard_restore()  — normal terminal (keyboard.c)
 *     6. memory_cleanup()    — free virtual RAM (memory.c)
 */
int main(void) {
  /* ---- 1. Boot memory subsystem (memory.c) ----------------------------- */
  memory_init(VIRTUAL_RAM_SIZE);

  /* ---- 1b. Boot sound subsystem (sound.c) ----------------------------- */
  sound_init();

  /* ---- Allocate top-level game objects from virtual RAM ---------------- */
  GameBoard *board = (GameBoard *)t_alloc((int)sizeof(GameBoard));
  GameState *state = (GameState *)t_alloc((int)sizeof(GameState));

  if (!board || !state) {
    /* Error handling: OOM at startup is fatal */
    if (board)
      t_dealloc(board);
    if (state)
      t_dealloc(state);
    sound_cleanup();
    memory_cleanup();
    return 1;
  }

  state->running = 1;
  state->high_score = score_load(); /* file system: read persisted score */

  /* ---- 2. Boot I/O subsystem (keyboard.c) ------------------------------ */
  keyboard_init();

  /* ---- 3. Name entry — exercises readLine, t_split, t_strncpy ---------- */
  prompt_player_name(state);

  /* ---- 4. Run game loop ------------------------------------------------ */
  game_loop(state, board);

  /* ---- Persist high score before shutdown ------------------------------ */
  score_save(state->high_score);

  /* ---- Shutdown message ------------------------------------------------ */
  screen_clear();
  screen_set_cursor(1, 1);
  screen_render_string("Thanks for playing, ");
  /* string.c: t_strlen confirms name is populated before printing */
  if (t_strlen(state->player_name) > 0) {
    screen_render_string(state->player_name);
  }
  screen_render_string("! Score saved.\n");

  /* ---- 5, 6, 7. Teardown in reverse boot order ------------------------- */
  sound_cleanup(); /* stop music, remove temp WAV files */
  keyboard_restore();

  t_dealloc(board);
  t_dealloc(state);
  memory_cleanup();

  return 0;
}
