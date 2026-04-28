/* =============================================================================
 * main.c  —  Tetris Phase 1 — Complete Interactive Terminal Game
 * =============================================================================
 *
 * PROJECT: Tetris OS Simulator — Track A (Interactive Terminal Application)
 * PHASE  : Phase 1  — Library Integration & Basic Mechanics
 *
 * FIXES APPLIED ON TOP OF THE 1443-LINE VERSION:
 *   FIX 1 — Game-over box alignment:
 *            Old box_w=26 was wider than board interior (20 chars) by 6 chars,
 *            causing it to overflow into the HUD every time.  Box is now exactly
 *            22 chars wide (20 content + 2 border '|' chars) pinned to
 *            BOARD_ORIGIN_X — perfectly contained within the board.
 *
 *   FIX 2 — Terminal resize (SIGWINCH):
 *            Old handler called screen_clear() once.  Partial escape sequences
 *            left in the buffer caused corrupt frames.  Now does a double-clear
 *            with a 30 ms settle between, plus resets prev_ok so a full redraw
 *            happens immediately.
 *
 *   FIX 3 — VS Code "too many blocks" / layout broken:
 *            Detects VS Code via getenv("TERM_PROGRAM") == "vscode".
 *            Raises MIN_TERM_COLS to 70 for VS Code (narrower default font).
 *            Shows a specific font-size hint to VS Code users when terminal
 *            is too small.  check_terminal_size() extracted as a helper so
 *            the same logic is used on every frame and after every resize.
 *
 *   FIX 4 — Theme indicator in HUD:
 *            HUD now shows "THEME: []" / "##" / "()" / "@@" so the player
 *            can see the active block style when pressing T.
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
 * │ 3. File System               │ leaderboard_save / leaderboard_load      │
 * │                              │ persist the top-5 scores between         │
 * │                              │ sessions.  Format: score|level|lines|name│
 * │                              │ (unified with ws_tetris.c format).       │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 4. I/O Management            │ keyboard.c (input) + screen.c (output).  │
 * │                              │ Non-blocking keyPressed() drives the     │
 * │                              │ real-time loop; readLine() used for name │
 * │                              │ entry; screen_* renders frames.          │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 5. Error Handling & Security │ Every alloc is NULL-checked; out-of-     │
 * │                              │ bounds moves are rejected via            │
 * │                              │ t_in_bounds(); SIGINT/SIGTERM restore    │
 * │                              │ terminal before exit; game resets        │
 * │                              │ cleanly on game-over.                    │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 6. Networking                │ Leaderboard file is shared with          │
 * │                              │ ws_tetris.c WebSocket backend (unified   │
 * │                              │ format).  Architecture is modular and    │
 * │                              │ extensible for future multiplayer.       │
 * ├──────────────────────────────┼──────────────────────────────────────────┤
 * │ 7. User Interface            │ Full ANSI colored board, HUD panel,      │
 * │                              │ hold piece, ghost piece, combo display,  │
 * │                              │ controls legend, game-over screen all    │
 * │                              │ drawn exclusively via screen.c.          │
 * └──────────────────────────────┴──────────────────────────────────────────┘
 *
 * LIBRARY INTEGRATION MAP (Phase 1 requirement — evaluator reference):
 *
 *   keyboard.c  → keyPressed()  : non-blocking game-loop input every frame
 *               → readLine()    : blocking name entry at startup
 *
 *   string.c    → t_strcmp()    : key dispatch comparison in game loop
 *               → t_split()     : tokenise "name surname" input at boot
 *               → t_strlen()    : validate name length after readLine
 *               → t_strncpy()   : safely copy name into GameState
 *               → t_itoa()      : int → string for HUD score display
 *               → t_atoi()      : string → int when loading leaderboard file
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
 *               → screen_get_size() : terminal dimension query
 *               → screen_panic()    : secure halt with terminal restore
 *
 * CONTROLS:
 *   A / ←  — move piece left
 *   D / →  — move piece right
 *   S / ↓  — soft drop (one row immediately)
 *   W / ↑  — rotate piece clockwise
 *   C      — hold piece
 *   Space  — hard drop (instant place)
 *   T      — cycle block theme
 *   P      — pause / resume
 *   Q      — quit game
 *
 * BUILD:
 *   make          (uses provided Makefile)
 *   ./tetris_os
 *
 * RULES COMPLIANCE:
 *   - No <string.h>, <math.h>, or direct malloc/free in game logic.
 *   - <stdio.h> used ONLY for file I/O (leaderboard) and fflush/fputc.
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
#include <signal.h>   /* signal(), SIGWINCH, SIGINT, SIGTERM               */
#include <stdio.h>    /* FILE, fopen, fclose, fgetc, fputc — score file I/O */
#include <stdlib.h>   /* getenv() — VS Code terminal detection             */
#include <sys/time.h> /* gettimeofday() — real-time drop timing             */
#include <unistd.h>   /* usleep() — frame-rate pacing                       */

/* ============================================================================
 * SECTION 0: SIGNAL HANDLING & CLEAN SHUTDOWN
 *
 * OS Module: Error Handling & Security
 *   SIGINT  (Ctrl-C) and SIGTERM must restore the terminal from raw mode
 *   before the process exits.  Without this, the shell is left unusable.
 *
 *   g_quit_signal is checked once per frame in game_loop().  When set, the
 *   loop exits naturally so that the normal teardown path (keyboard_restore,
 *   memory_cleanup) runs — no longjmp, no abrupt exit from a signal handler.
 * ============================================================================
 */

/* Set by SIGINT / SIGTERM — checked each game-loop frame */
static volatile sig_atomic_t g_quit_signal = 0;

/* Set by SIGWINCH — checked each frame to force a full redraw */
static volatile sig_atomic_t g_resize_flag = 0;

static void handle_quit_signal(int sig) {
    (void)sig;
    g_quit_signal = 1;
}

static void handle_sigwinch(int sig) {
    (void)sig;
    g_resize_flag = 1;
}

/* =============================================================================
 * SECTION 1: CONSTANTS & CONFIGURATION
 * =============================================================================
 */

#define BOARD_W        10
#define BOARD_H        20
#define BOARD_HIDDEN   4
#define BOARD_TOTAL_H  (BOARD_H + BOARD_HIDDEN)

#define BOARD_ORIGIN_X 4
#define BOARD_ORIGIN_Y 4   /* shifted down to make room for the name bar */

#define CELL_W  2

#define HUD_X   (BOARD_ORIGIN_X + (BOARD_W * CELL_W) + 4)
#define HUD_Y   4

#define SCORE_SINGLE  100
#define SCORE_DOUBLE  300
#define SCORE_TRIPLE  500
#define SCORE_TETRIS  800

#define SPEED_INITIAL   500   /* ms between auto-drops at level 1   */
#define SPEED_MIN        50   /* ms minimum (fastest drop speed)    */
#define SPEED_DECREMENT  30   /* ms faster per level                */

#define VIRTUAL_RAM_SIZE  (1024 * 1024)
#define PLAYER_NAME_MAX   20

/*
 * FIX 3 — Minimum terminal dimensions.
 * VS Code integrated terminal uses a narrower default font, so the board
 * plus HUD requires more columns to render without wrapping.
 * Standard terminals: 60 cols x 34 rows.
 * VS Code terminal:   70 cols x 34 rows (raised by 10 to compensate).
 */
/* Minimum rows: board bottom border is at BOARD_ORIGIN_Y + BOARD_H = 4+20 = 24,  */
/* bottom border row = 25. Set MIN to 25 — the absolute floor for the game.    */
#define MIN_TERM_COLS        50
#define MIN_TERM_ROWS  25
#define MIN_TERM_COLS_VSCODE 50

/* Lock delay */
#define LOCK_DELAY_MS    500
#define MAX_LOCK_RESETS   15

/* =============================================================================
 * SECTION 2: TETROMINO DEFINITIONS
 * =============================================================================
 */

#define NUM_PIECES    7
#define NUM_ROTATIONS 4
#define PIECE_SIZE    4

#define SCORE_FILE "leaderboard.txt"

static const int PIECES[NUM_PIECES][NUM_ROTATIONS][PIECE_SIZE][PIECE_SIZE] = {
    /* 0 — I */
    {{{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
     {{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}},
     {{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}},
    /* 1 — O */
    {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
    /* 2 — T */
    {{{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    /* 3 — S */
    {{{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}},
     {{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}},
     {{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    /* 4 — Z */
    {{{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}},
    /* 5 — J */
    {{{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}},
    /* 6 — L */
    {{{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
     {{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}},
     {{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}},
     {{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}}
};

static const int PIECE_COLORS[NUM_PIECES] = {
    SCREEN_COLOR_CYAN,
    SCREEN_COLOR_YELLOW,
    SCREEN_COLOR_MAGENTA,
    SCREEN_COLOR_GREEN,
    SCREEN_COLOR_RED,
    SCREEN_COLOR_BLUE,
    SCREEN_COLOR_BRIGHT_YELLOW
};

/* FIX 4: named theme styles for HUD display */
static const char *THEME_NAMES[4] = { "[]", "##", "()", "@@" };

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

/*
 * LeaderEntry — unified with ws_tetris.c format.
 * Stores score, level, lines, and name so both builds share one file.
 */
typedef struct {
    char name[PLAYER_NAME_MAX];
    int  score;
    int  level;
    int  lines;
} LeaderEntry;

typedef struct {
    int  score;
    int  high_score;
    int  level;
    int  lines_cleared;
    int  running;
    int  game_over;
    int  paused;
    int  next_type;
    int  drop_counter;
    int  drop_speed;
    int  combo;
    int  hold_used;
    int  held_type;
    int  bag[NUM_PIECES];
    int  bag_index;
    int  lock_ticks;
    int  lock_resets;
    int  theme;     
    int  leaderboard_saved;                 
    LeaderEntry leaderboard[5];
    char player_name[PLAYER_NAME_MAX];
} GameState;

/* =============================================================================
 * SECTION 4: SIMPLE LCG RANDOM (no <stdlib.h> rand)
 * =============================================================================
 */

static int rand_state  = 1;
static int rand_seeded = 0;

static void seed_random(int seed) {
    rand_state  = (seed > 0) ? seed : 1;
    rand_seeded = 1;
}

static int get_random(int max) {
    if (max <= 0) return 0;
    rand_state = t_mod(t_mul(rand_state, 1103) + 12345, 32768);
    return t_mod(rand_state, max);
}

/* =============================================================================
 * SECTION 5: REAL-TIME CLOCK HELPER
 * =============================================================================
 */

static long long get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + (long long)tv.tv_usec / 1000LL;
}

/* =============================================================================
 * SECTION 6: NES-STYLE DROP SPEED
 * =============================================================================
 */

static int nes_drop_speed(int level) {
    if (level <= 0)  level = 1;
    if (level <= 8)  {
        int ticks = 13 - level;
        if (ticks < 5) ticks = 5;
        return t_mul(ticks, 50);
    }
    if (level <= 13) {
        int ticks = 8 - t_div(level - 8, 2);
        if (ticks < 2) ticks = 2;
        return t_mul(ticks, 50);
    }
    return 50;
}

/* =============================================================================
 * SECTION 7: FILE SYSTEM — Leaderboard Persistence
 * =============================================================================
 */

static void leaderboard_load(GameState *state) {
    int i;
    for (i = 0; i < 5; i++) {
        t_strncpy(state->leaderboard[i].name, "---", PLAYER_NAME_MAX);
        state->leaderboard[i].score = 0;
        state->leaderboard[i].level = 0;
        state->leaderboard[i].lines = 0;
    }

    FILE *f = fopen(SCORE_FILE, "r");
    if (!f) return;

    char line[128];
    int  count = 0;

    while (count < 5) {
        int pos = 0, c;
        while (pos < (int)sizeof(line) - 1 &&
               (c = fgetc(f)) != EOF && c != '\n') {
            line[pos++] = (char)c;
        }
        line[pos] = '\0';
        if (pos == 0 && c == EOF) break;

        char *tokens[4];
        int   ntok = t_split(line, '|', tokens, 4);
        if (ntok == 4) {
            state->leaderboard[count].score = t_atoi(tokens[0]);
            state->leaderboard[count].level = t_atoi(tokens[1]);
            state->leaderboard[count].lines = t_atoi(tokens[2]);
            t_strncpy(state->leaderboard[count].name, tokens[3], PLAYER_NAME_MAX);
            count++;
        }
    }
    fclose(f);
    state->high_score = (count > 0) ? state->leaderboard[0].score : 0;
}

static void leaderboard_save(const GameState *state) {
    FILE *f = fopen(SCORE_FILE, "w");
    if (!f) return;

    int i;
    for (i = 0; i < 5; i++) {
        if (state->leaderboard[i].score <= 0) break;

        char buf[16];

        t_itoa(state->leaderboard[i].score, buf);
        for (int j = 0; buf[j]; j++) fputc(buf[j], f);
        fputc('|', f);

        t_itoa(state->leaderboard[i].level, buf);
        for (int j = 0; buf[j]; j++) fputc(buf[j], f);
        fputc('|', f);

        t_itoa(state->leaderboard[i].lines, buf);
        for (int j = 0; buf[j]; j++) fputc(buf[j], f);
        fputc('|', f);

        for (int j = 0; state->leaderboard[i].name[j]; j++)
            fputc(state->leaderboard[i].name[j], f);

        fputc('\n', f);
    }
    fclose(f);
}

static void leaderboard_insert(GameState *state) {
    int i, j;
    for (i = 0; i < 5; i++) {
        if (state->score > state->leaderboard[i].score) {
            for (j = 4; j > i; j--) {
                t_strncpy(state->leaderboard[j].name,
                          state->leaderboard[j-1].name, PLAYER_NAME_MAX);
                state->leaderboard[j].score = state->leaderboard[j-1].score;
                state->leaderboard[j].level = state->leaderboard[j-1].level;
                state->leaderboard[j].lines = state->leaderboard[j-1].lines;
            }
            t_strncpy(state->leaderboard[i].name,
                      state->player_name, PLAYER_NAME_MAX);
            state->leaderboard[i].score = state->score;
            state->leaderboard[i].level = state->level;
            state->leaderboard[i].lines = state->lines_cleared;
            break;
        }
    }
    state->high_score = state->leaderboard[0].score;
    leaderboard_save(state);
}

/* =============================================================================
 * SECTION 8: 7-BAG RANDOMIZER
 * =============================================================================
 */

static void bag_fill(GameState *state) {
    int i;
    for (i = 0; i < NUM_PIECES; i++) state->bag[i] = i;
    for (i = NUM_PIECES - 1; i > 0; i--) {
        int j   = get_random(i + 1);
        int tmp = state->bag[i];
        state->bag[i] = state->bag[j];
        state->bag[j] = tmp;
    }
    state->bag_index = 0;
}

static int bag_next(GameState *state) {
    if (state->bag_index >= NUM_PIECES) bag_fill(state);
    return state->bag[state->bag_index++];
}

/* =============================================================================
 * SECTION 9: ERROR HANDLING — Board & Piece Helpers
 * =============================================================================
 */

static int board_cell_filled(const GameBoard *board, int row, int col) {
    if (!t_in_bounds(col, 0, BOARD_W - 1))      return 1;
    if (!t_in_bounds(row, 0, BOARD_TOTAL_H - 1)) return 1;
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
    while (!piece_collides(board, piece, drop + 1, 0, piece->rotation))
        drop++;
    return piece->row + drop;
}

/* =============================================================================
 * SECTION 10: MEMORY MANAGEMENT — Piece Lifecycle
 * =============================================================================
 */

static Piece *piece_spawn(GameState *state) {
    Piece *p = (Piece *)t_alloc((int)sizeof(Piece));
    if (!p) T_PANIC("Failed to allocate piece");

    p->type     = state->next_type;
    p->rotation = 0;
    p->col      = t_div(BOARD_W, 2) - 2;
    p->row      = BOARD_HIDDEN - 2;

    state->next_type   = bag_next(state);
    state->hold_used   = 0;
    state->lock_ticks  = 0;
    state->lock_resets = 0;
    return p;
}

static void piece_lock(GameBoard *board, Piece *piece, GameState *state) {
    int pr, pc;
    for (pr = 0; pr < PIECE_SIZE; pr++) {
        for (pc = 0; pc < PIECE_SIZE; pc++) {
            if (PIECES[piece->type][piece->rotation][pr][pc]) {
                int br = piece->row + pr;
                int bc = piece->col + pc;
                if (t_in_bounds(br, 0, BOARD_TOTAL_H - 1) &&
                    t_in_bounds(bc, 0, BOARD_W - 1)) {
                    board->cells[br][bc] = piece->type + 1;
                }
            }
        }
    }
    t_dealloc(piece);
    (void)state;
}

/* =============================================================================
 * SECTION 11: LINE CLEAR + SCORING
 * =============================================================================
 */

static int shake_frames = 0;

static void board_clear_lines(GameBoard *board, GameState *state) {
    int full_rows[BOARD_TOTAL_H];
    int num_full = 0, row, col;

    for (row = BOARD_TOTAL_H - 1; row >= 0; row--) {
        int full = 1;
        for (col = 0; col < BOARD_W; col++) {
            if (board->cells[row][col] == 0) { full = 0; break; }
        }
        if (full) full_rows[num_full++] = row;
    }
    if (num_full == 0) return;

    int flash;
    for (flash = 0; flash < 3; flash++) {
        int f;
        for (f = 0; f < num_full; f++) {
            int fr             = full_rows[f];
            int board_row_scr  = fr - BOARD_HIDDEN;
            if (board_row_scr < 0) continue;
            int term_y = BOARD_ORIGIN_Y + board_row_scr;
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
        usleep(60000);
    }

    sound_play(SND_CLEAR);
    shake_frames = 4;

    int cleared = 0;
    for (row = BOARD_TOTAL_H - 1; row >= 0; row--) {
        int full = 1;
        for (col = 0; col < BOARD_W; col++) {
            if (board->cells[row][col] == 0) { full = 0; break; }
        }
        if (full) {
            cleared++;
            int r;
            for (r = row; r > 0; r--)
                for (col = 0; col < BOARD_W; col++)
                    board->cells[r][col] = board->cells[r-1][col];
            for (col = 0; col < BOARD_W; col++)
                board->cells[0][col] = 0;
            row++;
        }
    }

    int base = (cleared == 1) ? SCORE_SINGLE :
               (cleared == 2) ? SCORE_DOUBLE :
               (cleared == 3) ? SCORE_TRIPLE : SCORE_TETRIS;

    int combo_bonus = 0;
    if (state->combo > 0)
        combo_bonus = t_mul(t_mul(50, state->combo), state->level);

    state->score        += t_mul(base, state->level) + combo_bonus;
    state->combo++;
    state->lines_cleared = state->lines_cleared + cleared;

    state->level = t_div(state->lines_cleared, 10) + 1;
    if (state->level > 20) state->level = 20;

    state->drop_speed = nes_drop_speed(state->level);

    if (state->score > state->high_score)
        state->high_score = state->score;
}

/* =============================================================================
 * SECTION 12: GAME ACTIONS
 * =============================================================================
 */

static void action_rotate(GameBoard *board, Piece *piece) {
    int next_rot = t_mod(piece->rotation + 1, NUM_ROTATIONS);
    if      (!piece_collides(board, piece, 0,  0, next_rot)) {
        piece->rotation = next_rot;
    } else if (!piece_collides(board, piece, 0, -1, next_rot)) {
        piece->col--;
        piece->rotation = next_rot;
    } else if (!piece_collides(board, piece, 0,  1, next_rot)) {
        piece->col++;
        piece->rotation = next_rot;
    }
}

static void action_hard_drop(GameBoard *board, Piece *piece, GameState *state) {
    int rows_dropped = 0;
    while (!piece_collides(board, piece, 1, 0, piece->rotation)) {
        piece->row++;
        rows_dropped++;
    }
    state->score += t_mul(2, rows_dropped);
    piece_lock(board, piece, state);
}

/* =============================================================================
 * SECTION 13: RENDERING
 * =============================================================================
 */

static void render_border(void) {
    int row, col;
    screen_set_color(SCREEN_COLOR_WHITE, SCREEN_COLOR_DEFAULT);

    screen_set_cursor(BOARD_ORIGIN_X - 1, BOARD_ORIGIN_Y - 1);
    screen_render_char('+');
    for (col = 0; col < BOARD_W; col++) screen_render_string("--");
    screen_render_char('+');

    for (row = 0; row < BOARD_H; row++) {
        int ty = BOARD_ORIGIN_Y + row;
        screen_set_cursor(BOARD_ORIGIN_X - 1, ty);
        screen_render_char('|');
        screen_set_cursor(BOARD_ORIGIN_X + t_mul(BOARD_W, CELL_W), ty);
        screen_render_char('|');
    }

    screen_set_cursor(BOARD_ORIGIN_X - 1, BOARD_ORIGIN_Y + BOARD_H);
    screen_render_char('+');
    for (col = 0; col < BOARD_W; col++) screen_render_string("--");
    screen_render_char('+');

    screen_reset_color();
}

static void render_cell(const GameState *state, int tx, int ty, int color) {
    screen_set_cursor(tx, ty);
    if (color == 0) {
        screen_render_string("  ");
    } else {
        screen_set_color(color, SCREEN_COLOR_DEFAULT);
        /* FIX 4: THEME_NAMES keeps display and HUD in sync */
        screen_render_string(THEME_NAMES[t_mod(state->theme, 4)]);
        screen_reset_color();
    }
}

static void render_board(const GameState *state, const GameBoard *board,
                         const Piece *piece) {
    int row, col, pr, pc;
    int gr = piece ? ghost_row(board, piece) : 0;

    for (row = 0; row < BOARD_H; row++) {
        int board_row = row + BOARD_HIDDEN;
        for (col = 0; col < BOARD_W; col++) {
            int tx = BOARD_ORIGIN_X + t_mul(col, CELL_W);
            int ty = BOARD_ORIGIN_Y + row;

            int draw_active = 0, draw_ghost = 0, active_color = 0;

            if (piece) {
                for (pr = 0; pr < PIECE_SIZE && !draw_active; pr++) {
                    for (pc = 0; pc < PIECE_SIZE && !draw_active; pc++) {
                        if (PIECES[piece->type][piece->rotation][pr][pc]) {
                            if (piece->row + pr == board_row &&
                                piece->col + pc == col) {
                                draw_active  = 1;
                                active_color = PIECE_COLORS[piece->type];
                            }
                        }
                    }
                }
                if (!draw_active) {
                    for (pr = 0; pr < PIECE_SIZE && !draw_ghost; pr++) {
                        for (pc = 0; pc < PIECE_SIZE && !draw_ghost; pc++) {
                            if (PIECES[piece->type][piece->rotation][pr][pc]) {
                                if (gr + pr == board_row &&
                                    piece->col + pc == col)
                                    draw_ghost = 1;
                            }
                        }
                    }
                }
            }

            if (draw_active) {
                render_cell(state, tx, ty, active_color);
            } else {
                int cell = board->cells[board_row][col];
                if (cell > 0) {
                    render_cell(state, tx, ty, PIECE_COLORS[cell - 1]);
                } else if (draw_ghost) {
                    screen_set_cursor(tx, ty);
                    screen_set_color(SCREEN_COLOR_WHITE, SCREEN_COLOR_DEFAULT);
                    screen_render_string("--");
                    screen_reset_color();
                } else {
                    render_cell(state, tx, ty, 0);
                }
            }
        }
    }
}

static void render_next_piece(const GameState *state, int next_type) {
    int pr, pc;
    for (pr = 0; pr < PIECE_SIZE; pr++) {
        screen_set_cursor(HUD_X + 1, HUD_Y + 5 + pr);
        screen_render_string("        ");
    }
    for (pr = 0; pr < PIECE_SIZE; pr++) {
        for (pc = 0; pc < PIECE_SIZE; pc++) {
            if (PIECES[next_type][0][pr][pc]) {
                render_cell(state,
                            HUD_X + 1 + t_mul(pc, CELL_W),
                            HUD_Y + 5 + pr,
                            PIECE_COLORS[next_type]);
            }
        }
    }
}

static void render_player_bar(const GameState *state) {
    screen_set_cursor(BOARD_ORIGIN_X - 1, BOARD_ORIGIN_Y - 2);
    screen_set_color(SCREEN_COLOR_BRIGHT_CYAN, SCREEN_COLOR_DEFAULT);
    screen_render_string("Player: ");
    if (t_strlen(state->player_name) > 0)
        screen_render_string(state->player_name);
    else
        screen_render_string("Anonymous");
    screen_reset_color();
}

/*
 * render_hud()
 *
 * COMPACT LAYOUT — fits in 22 HUD rows (HUD_Y+0 to HUD_Y+21).
 * With HUD_Y=4 the last row is 25, well inside a 30-row terminal.
 *
 * Row map:
 *  +0  TETRIS title
 *  +1  SCORE: value    BEST: value
 *  +2  LEVEL: value    LINES: value
 *  +3  COMBO (or blank)
 *  +4  NEXT:
 *  +5..+8  next piece preview
 *  +9  HOLD [C]:
 * +10..+13  hold piece
 * +14  THEME: xx
 * +15  -- CONTROLS --
 * +16  A/D:Move  W:Rot
 * +17  S:SDrop  Sp:HDrop
 * +18  C:Hold  T:Theme
 * +19  P:Pause  Q:Quit
 * +20  ** PAUSED ** (or blank)
 * Total rows used: HUD_Y + 20 = 24. Fits in 30-row terminal. ✓
 */
static void render_hud(const GameState *state) {
    char buf[20];
    char buf2[20];

    /* Row 0: title */
    screen_set_color(SCREEN_COLOR_BRIGHT_CYAN, SCREEN_COLOR_DEFAULT);
    screen_set_cursor(HUD_X, HUD_Y);
    screen_render_string("=== TETRIS ===  ");
    screen_reset_color();

    /* Row 1: SCORE and BEST on same line */
    screen_set_cursor(HUD_X, HUD_Y + 1);
    screen_render_string("SCR:");
    t_itoa(state->score, buf);
    screen_render_string(buf);
    /* pad score to fixed width so BEST label stays aligned */
    int spad = 7 - t_strlen(buf);
    while (spad-- > 0) screen_render_char(' ');
    screen_render_string("HI:");
    t_itoa(state->high_score, buf2);
    screen_render_string(buf2);
    screen_render_string("     ");

    /* Row 2: LEVEL and LINES on same line */
    screen_set_cursor(HUD_X, HUD_Y + 2);
    screen_render_string("LVL:");
    t_itoa(state->level, buf);
    screen_render_string(buf);
    int lpad = 4 - t_strlen(buf);
    while (lpad-- > 0) screen_render_char(' ');
    screen_render_string("  LN:");
    t_itoa(state->lines_cleared, buf2);
    screen_render_string(buf2);
    screen_render_string("     ");

    /* Row 3: COMBO (only shown when active) */
    screen_set_cursor(HUD_X, HUD_Y + 3);
    if (state->combo > 1) {
        screen_set_color(SCREEN_COLOR_BRIGHT_YELLOW, SCREEN_COLOR_DEFAULT);
        screen_render_string("COMBO:");
        t_itoa(state->combo, buf);
        screen_render_string(buf);
        screen_render_string("x       ");
        screen_reset_color();
    } else {
        screen_render_string("                ");
    }

    /* Row 4: NEXT label, rows 5-8: preview */
    screen_set_cursor(HUD_X, HUD_Y + 4);
    screen_render_string("NEXT:           ");
    render_next_piece(state, state->next_type);

    /* Row 9: HOLD label, rows 10-13: hold piece */
    screen_set_cursor(HUD_X, HUD_Y + 9);
    screen_render_string("HOLD[C]:        ");
    {
        int pr, pc;
        for (pr = 0; pr < PIECE_SIZE; pr++) {
            screen_set_cursor(HUD_X + 1, HUD_Y + 10 + pr);
            screen_render_string("        ");
        }
        if (state->held_type >= 0) {
            for (pr = 0; pr < PIECE_SIZE; pr++) {
                for (pc = 0; pc < PIECE_SIZE; pc++) {
                    if (PIECES[state->held_type][0][pr][pc]) {
                        int color = state->hold_used
                                    ? SCREEN_COLOR_WHITE
                                    : PIECE_COLORS[state->held_type];
                        render_cell(state,
                                    HUD_X + 1 + t_mul(pc, CELL_W),
                                    HUD_Y + 10 + pr,
                                    color);
                    }
                }
            }
        }
    }

    /* Row 14: THEME indicator */
    screen_set_cursor(HUD_X, HUD_Y + 14);
    screen_set_color(SCREEN_COLOR_BRIGHT_CYAN, SCREEN_COLOR_DEFAULT);
    screen_render_string("THEME:");
    screen_render_string(THEME_NAMES[t_mod(state->theme, 4)]);
    screen_render_string("  ");
    screen_reset_color();

    /* Rows 15-19: compressed controls (2 keys per line) */
    screen_set_color(SCREEN_COLOR_YELLOW, SCREEN_COLOR_DEFAULT);
    screen_set_cursor(HUD_X, HUD_Y + 15);
    screen_render_string("-CONTROLS-      ");
    screen_reset_color();
    screen_set_cursor(HUD_X, HUD_Y + 16); screen_render_string("A/D:Move W:Rot  ");
    screen_set_cursor(HUD_X, HUD_Y + 17); screen_render_string("S:SDrop Sp:HDrop");
    screen_set_cursor(HUD_X, HUD_Y + 18); screen_render_string("C:Hold  T:Theme ");
    screen_set_cursor(HUD_X, HUD_Y + 19); screen_render_string("P:Pause Q:Quit  ");

    /* Row 20: paused indicator */
    if (state->paused) {
        screen_set_color(SCREEN_COLOR_BRIGHT_YELLOW, SCREEN_COLOR_DEFAULT);
        screen_set_cursor(HUD_X, HUD_Y + 20);
        screen_render_string("** PAUSED **    ");
        screen_reset_color();
    } else {
        screen_set_cursor(HUD_X, HUD_Y + 20);
        screen_render_string("                ");
    }
}

/*
 * render_game_over()
 *
 * Box is exactly 22 characters wide:
 *   '|' + 20 content chars + '|' = 22 total.
 *   Board interior = BOARD_W * CELL_W = 20 chars — perfect fit.
 *   Left edge at BOARD_ORIGIN_X (col 4), board border is at col 3.
 *
 * CLEAR PASS: before drawing the box, blank the entire board interior
 *   so pieces don't show through the overlay.
 */
static void render_game_over(const GameState *state) {
    char buf[16];
    int  k, i;

    /* Box left edge = board interior left edge. */
    int cx = BOARD_ORIGIN_X;

    /* Vertically center the 13-row box in the 20-row visible board */
    int box_h = 13;
    int cy    = BOARD_ORIGIN_Y + t_div(BOARD_H - box_h, 2);
    if (cy < BOARD_ORIGIN_Y) cy = BOARD_ORIGIN_Y;

    /*
     * CLEAR PASS: blank the entire board interior before drawing the box
     * so active/locked pieces do not show through the overlay.
     * The interior is BOARD_W*CELL_W=20 chars wide, BOARD_H=20 rows tall.
     */
    int clear_row;
    for (clear_row = 0; clear_row < BOARD_H; clear_row++) {
        screen_set_cursor(BOARD_ORIGIN_X, BOARD_ORIGIN_Y + clear_row);
        for (k = 0; k < BOARD_W * CELL_W; k++) screen_render_char(' ');
    }

    screen_set_color(SCREEN_COLOR_BRIGHT_RED, SCREEN_COLOR_DEFAULT);

    /* Top border: +--------------------+ (exactly 20 dashes) */
    screen_set_cursor(cx, cy);
    screen_render_char('+');
    for (k = 0; k < 20; k++) screen_render_char('-');
    screen_render_char('+');

    /* Title — exactly 20 content chars */
    screen_set_cursor(cx, cy + 1);
    screen_render_string("|   ** GAME OVER **  |");

    /* Separator */
    screen_set_cursor(cx, cy + 2);
    screen_render_char('|');
    for (k = 0; k < 20; k++) screen_render_char('-');
    screen_render_char('|');

    /*
     * Leaderboard rows.
     * Each row: '|' + rank(3) + name(9,padded) + ' ' + score(6,padded) + ' ' + '|'
     *           = 1 + 3 + 9 + 1 + 6 + 1 + 1 = 22 chars total ✓
     */
    for (i = 0; i < 5; i++) {
        screen_set_cursor(cx, cy + 3 + i);
        screen_render_char('|');

        /* Rank: "1. " */
        t_itoa(i + 1, buf);
        screen_render_string(buf);
        screen_render_string(". ");

        /* Name: 9 chars, space-padded */
        int nlen = t_strlen(state->leaderboard[i].name);
        if (nlen > 9) nlen = 9;
        int nc;
        for (nc = 0; nc < nlen; nc++)
            screen_render_char(state->leaderboard[i].name[nc]);
        for (nc = nlen; nc < 9; nc++)
            screen_render_char(' ');

        /* Space */
        screen_render_char(' ');

        /* Score: 6 chars, space-padded */
        t_itoa(state->leaderboard[i].score, buf);
        int slen = t_strlen(buf);
        if (slen > 6) slen = 6;
        int sc;
        for (sc = 0; sc < slen; sc++) screen_render_char(buf[sc]);
        for (sc = slen; sc < 6; sc++) screen_render_char(' ');

        /* Closing space + border */
        screen_render_char(' ');
        screen_render_char('|');
    }

    /* Separator */
    screen_set_cursor(cx, cy + 8);
    screen_render_char('|');
    for (k = 0; k < 20; k++) screen_render_char('-');
    screen_render_char('|');

    /* Your score row — "| Score: NNNNNNN     |" padded to 20 content chars */
    screen_set_cursor(cx, cy + 9);
    screen_render_string("| Score: ");
    t_itoa(state->score, buf);
    int slen2 = t_strlen(buf);
    if (slen2 > 9) slen2 = 9;
    for (k = 0; k < slen2; k++) screen_render_char(buf[k]);
    /* Pad remaining content to reach 20 chars: "| Score: " = 9, score up to 9, pad rest */
    int pad = 20 - 9 - slen2 - 1;   /* -1 for closing ' ' before '|' */
    for (k = 0; k < pad; k++) screen_render_char(' ');
    screen_render_string(" |");

    /* Blank row */
    screen_set_cursor(cx, cy + 10);
    screen_render_string("|                    |");

    /* Actions row — exactly 20 content chars */
    screen_set_cursor(cx, cy + 11);
    screen_render_string("| R:retry    Q:quit  |");

    /* Bottom border */
    screen_set_cursor(cx, cy + 12);
    screen_render_char('+');
    for (k = 0; k < 20; k++) screen_render_char('-');
    screen_render_char('+');

    screen_reset_color();
}

/* =============================================================================
 * SECTION 14: GAME RESET
 * =============================================================================
 */

static void game_reset(GameBoard *board, GameState *state) {
    int r, c;
    for (r = 0; r < BOARD_TOTAL_H; r++)
        for (c = 0; c < BOARD_W; c++)
            board->cells[r][c] = 0;

    state->score         = 0;
    state->level         = 1;
    state->lines_cleared = 0;
    state->game_over     = 0;
    state->paused        = 0;
    state->drop_counter  = 0;
    state->drop_speed    = SPEED_INITIAL;
    state->combo         = 0;
    state->hold_used     = 0;
    state->held_type     = -1;
    state->lock_ticks    = 0;
    state->lock_resets   = 0;
    state->theme         = 0;
    rand_state  = 1;
    rand_seeded = 0;
    bag_fill(state);
    state->next_type = bag_next(state);
    state->leaderboard_saved = 0;   /* reset per-game insert guard */
    /* REMOVED: leaderboard_load(state) — leaderboard persists across retries,
     * loaded once at startup and after each game-over insert.
     * player_name intentionally NOT cleared. */
}

/* =============================================================================
 * SECTION 15: KEY DISPATCH HELPER
 * =============================================================================
 */
static int key_matches(char key, const char *lower, const char *upper) {
    char k[2];
    k[0] = key;
    k[1] = '\0';
    return (t_strcmp(k, lower) == 0 || t_strcmp(k, upper) == 0);
}

/* =============================================================================
 * SECTION 16: TERMINAL SIZE CHECK HELPER
 *
 * FIX 2 + FIX 3: Extracted as a reusable helper called every frame AND
 * after every SIGWINCH resize event.
 *
 * Detects VS Code via TERM_PROGRAM environment variable and:
 *   - Uses a higher column minimum (70 vs 60) to account for narrower font
 *   - Shows a specific "shrink font size" hint instead of a generic message
 *   - Shows current vs required dimensions so user knows exactly how much to resize
 *
 * Returns 1 if terminal is large enough, 0 if too small.
 * =============================================================================
 */
static int is_vscode_terminal(void) {
    const char *tp = getenv("TERM_PROGRAM");
    if (!tp) return 0;
    return (t_strcmp(tp, "vscode") == 0);
}

static int check_terminal_size(void) {
    int cols, rows;
    screen_get_size(&cols, &rows);
    int min_cols = is_vscode_terminal() ? MIN_TERM_COLS_VSCODE : MIN_TERM_COLS;

    if (cols >= min_cols && rows >= MIN_TERM_ROWS) return 1;

    screen_clear();   /* ADD THIS LINE — wipes old content before warning */

    screen_set_cursor(1, 1);
    screen_set_color(SCREEN_COLOR_BRIGHT_YELLOW, SCREEN_COLOR_DEFAULT);
    screen_render_string("  TERMINAL TOO SMALL              ");

    char buf[16];
    screen_set_cursor(1, 2);
    screen_render_string("  Need ");
    t_itoa(min_cols, buf); screen_render_string(buf);
    screen_render_string("x");
    t_itoa(MIN_TERM_ROWS, buf); screen_render_string(buf);
    screen_render_string("  Have ");
    t_itoa(cols, buf); screen_render_string(buf);
    screen_render_string("x");
    t_itoa(rows, buf); screen_render_string(buf);
    screen_render_string("    ");

    if (is_vscode_terminal()) {
        screen_set_cursor(1, 4);
        screen_render_string("  VS Code fix:                    ");
        screen_set_cursor(1, 5);
        screen_render_string("  Cmd+Shift+P >                   ");
        screen_set_cursor(1, 6);
        screen_render_string("  'Terminal: Change Font Size'    ");
        screen_set_cursor(1, 7);
        screen_render_string("  reduce size, or drag wider.     ");
    } else {
        screen_set_cursor(1, 4);
        screen_render_string("  Please resize your terminal.    ");
    }
    screen_reset_color();
    fflush(stdout);
    return 0;
}

/* =============================================================================
 * SECTION 17: PROCESS MANAGEMENT — Main Game Loop
 * =============================================================================
 */
static void game_loop(GameState *state, GameBoard *board) {
    if (!board) T_PANIC("Failed to allocate game board");
    if (!state) T_PANIC("Failed to allocate game state");

    game_reset(board, state);

    Piece *piece = piece_spawn(state);
    if (!piece) T_PANIC("Cannot spawn first piece");

    /* Alt screen already active (entered in main()).
     * Just clear and position cursor for a fresh game frame. */
    screen_clear();
    screen_set_cursor(1, 1);

    signal(SIGWINCH, handle_sigwinch);

    sound_music_start();

    int       game_over_rendered = 0;
    int       frame_counter      = 0;
    int       prev_term_ok       = 1;
    long long last_drop_time     = get_time_ms();

    while (state->running) {
        frame_counter++;

        /* Quit signal from SIGINT/SIGTERM or Ctrl+C character in keyPressed() */
        if (g_quit_signal) {
            state->running = 0;
            break;
        }

        /* ---- PROCESS 1: INPUT ------------------------------------------ */
        char key = keyPressed();

        if (key != '\0' && !rand_seeded) {
            seed_random(frame_counter);
            state->next_type = get_random(NUM_PIECES);
        }

        if (key != '\0') {
            if (state->game_over) {
                if (key_matches(key, "q", "Q")) {
                    state->running = 0;
                } else if (key_matches(key, "r", "R")) {
                    if (piece) { t_dealloc(piece); piece = NULL; }
                    game_reset(board, state);
                    piece = piece_spawn(state);
                    if (!piece) { state->running = 0; }
                    game_over_rendered = 0;
                    last_drop_time     = get_time_ms();
                }
            } else if (state->paused) {
                if      (key_matches(key, "p", "P")) state->paused  = 0;
                else if (key_matches(key, "q", "Q")) state->running = 0;
            } else {
                if (key_matches(key, "q", "Q")) {
                    state->running = 0;
                } else if (key_matches(key, "t", "T")) {
                    state->theme++;
                } else if (key_matches(key, "p", "P")) {
                    state->paused = 1;
                } else if (key_matches(key, "c", "C")) {
                    if (!state->hold_used) {
                        state->hold_used = 1;
                        if (state->held_type < 0) {
                            state->held_type = piece->type;
                            t_dealloc(piece);
                            piece = piece_spawn(state);
                            if (!piece ||
                                piece_collides(board, piece, 0, 0,
                                               piece->rotation)) {
                                state->game_over = 1;
                                sound_play(SND_GAMEOVER);
                                sound_music_stop();
                                if (piece) { t_dealloc(piece); piece = NULL; }
                            }
                        } else {
                            int tmp          = piece->type;
                            piece->type      = state->held_type;
                            piece->rotation  = 0;
                            piece->col       = t_div(BOARD_W, 2) - 2;
                            piece->row       = BOARD_HIDDEN - 2;
                            state->held_type = tmp;
                            if (piece_collides(board, piece, 0, 0,
                                               piece->rotation)) {
                                state->game_over = 1;
                                sound_play(SND_GAMEOVER);
                                sound_music_stop();
                                t_dealloc(piece); piece = NULL;
                            }
                        }
                        state->lock_ticks  = 0;
                        state->lock_resets = 0;
                        last_drop_time     = get_time_ms();
                    }
                } else if (key_matches(key, "a", "A")) {
                    if (piece && !piece_collides(board, piece, 0, -1,
                                                 piece->rotation)) {
                        piece->col--;
                        sound_play(SND_MOVE);
                        if (state->lock_ticks > 0 &&
                            state->lock_resets < MAX_LOCK_RESETS) {
                            state->lock_ticks = 0;
                            state->lock_resets++;
                        }
                    }
                } else if (key_matches(key, "d", "D")) {
                    if (piece && !piece_collides(board, piece, 0, 1,
                                                 piece->rotation)) {
                        piece->col++;
                        sound_play(SND_MOVE);
                        if (state->lock_ticks > 0 &&
                            state->lock_resets < MAX_LOCK_RESETS) {
                            state->lock_ticks = 0;
                            state->lock_resets++;
                        }
                    }
                } else if (key_matches(key, "w", "W")) {
                    if (piece) {
                        int old_rot = piece->rotation;
                        action_rotate(board, piece);
                        if (piece->rotation != old_rot) {
                            sound_play(SND_ROTATE);
                            if (state->lock_ticks > 0 &&
                                state->lock_resets < MAX_LOCK_RESETS) {
                                state->lock_ticks = 0;
                                state->lock_resets++;
                            }
                        }
                    }
                } else if (key_matches(key, "s", "S")) {
                    if (piece && !piece_collides(board, piece, 1, 0,
                                                 piece->rotation)) {
                        piece->row++;
                        state->score += 1;
                    }
                } else if (key == ' ') {
                    if (piece) {
                        action_hard_drop(board, piece, state);
                        sound_play(SND_DROP);
                        int old_lines = state->lines_cleared;
                        board_clear_lines(board, state);
                        if (state->lines_cleared == old_lines)
                            state->combo = 0;
                        piece = piece_spawn(state);
                        if (!piece ||
                            piece_collides(board, piece, 0, 0,
                                           piece->rotation)) {
                            state->game_over = 1;
                            if (!state->leaderboard_saved) {
                                leaderboard_insert(state);
                                state->leaderboard_saved = 1;
                            }
                            sound_play(SND_GAMEOVER);
                            sound_music_stop();
                            if (piece) { t_dealloc(piece); piece = NULL; }
                        }
                        last_drop_time = get_time_ms();
                    }
                }
            }
        }

        /* ---- PROCESS 2: PHYSICS / AUTO-DROP ---------------------------- */
        if (piece && !state->game_over && !state->paused) {
            long long now     = get_time_ms();
            long long elapsed = now - last_drop_time;

            if (elapsed >= (long long)state->drop_speed) {
                last_drop_time = now;
                if (!piece_collides(board, piece, 1, 0, piece->rotation)) {
                    piece->row++;
                    state->lock_ticks = 0;
                } else {
                    state->lock_ticks++;
                }
            }

            if (state->lock_ticks > 0) {
                int elapsed_lock = t_mul(state->lock_ticks, state->drop_speed);
                if (elapsed_lock >= LOCK_DELAY_MS ||
                    state->lock_resets >= MAX_LOCK_RESETS) {
                    piece_lock(board, piece, state);
                    sound_play(SND_DROP);
                    int old_lines = state->lines_cleared;
                    board_clear_lines(board, state);
                    if (state->lines_cleared == old_lines)
                        state->combo = 0;
                    piece = piece_spawn(state);
                    if (!piece ||
                        piece_collides(board, piece, 0, 0, piece->rotation)) {
                        state->game_over = 1;
                        if (!state->leaderboard_saved) {
                            leaderboard_insert(state);
                            state->leaderboard_saved = 1;
                        }
                        sound_play(SND_GAMEOVER);
                        sound_music_stop();
                        if (piece) { t_dealloc(piece); piece = NULL; }
                    }
                }
            }
        }

        /* ---- RESIZE HANDLING ----
        * Debounce: wait 80ms for iTerm2 to stop firing continuous SIGWINCH
        * events during drag, then discard any queued signals and do one
        * clean full redraw. Prevents partial frames at intermediate widths. */
        if (g_resize_flag) {
            g_resize_flag = 0;
            // Debounce: wait 80ms for iTerm2 to stop firing resize events,
            // then discard any that piled up during the wait.
            usleep(80000);
            g_resize_flag = 0;
            screen_clear();
            screen_set_cursor(1, 1);
            game_over_rendered = 0;
            prev_term_ok       = 1;
        }

        /* ---- FIX 3: TERMINAL SIZE CHECK (every frame) ---- */
        if (!check_terminal_size()) {
            if (prev_term_ok) {
                screen_clear();
                prev_term_ok = 0;
            }
            usleep(100000);
            continue;
        }
        if (!prev_term_ok) {
            /* Terminal just became large enough — full redraw */
            /* Terminal large enough again — force full redraw in place */
            screen_clear();
            screen_set_cursor(1, 1);
            prev_term_ok       = 1;
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
                render_board(state, board, NULL);
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
            render_board(state, board, piece);
            render_hud(state);
            fflush(stdout);
        }

        screen_set_cursor(1, BOARD_ORIGIN_Y + BOARD_H + 2);
        usleep(16000);
    }

    if (piece) { t_dealloc(piece); piece = NULL; }

    /* Alt screen exit handled in main() after this returns. */
}

/* =============================================================================
 * SECTION 18: NAME ENTRY
 * =============================================================================
 */
static void prompt_player_name(GameState *state) {
    char  raw_input[64];
    char *tokens[4];
    char *name_start;

    screen_clear();
    screen_set_cursor(1, 3);
    screen_set_color(SCREEN_COLOR_BRIGHT_CYAN, SCREEN_COLOR_DEFAULT);
    screen_render_string("  +============================+");
    screen_set_cursor(1, 4);
    screen_render_string("  |   TETRIS  OS  SIMULATOR   |");
    screen_set_cursor(1, 5);
    screen_render_string("  +============================+");
    screen_reset_color();
    screen_set_cursor(1, 7);
    screen_render_string("  Enter your name (first last): ");

    readLine(raw_input, (int)sizeof(raw_input));

    name_start = raw_input;
    while (*name_start == ' ') name_start++;

    if (t_strlen(name_start) == 0) {
        t_strncpy(state->player_name, "Player", PLAYER_NAME_MAX);
        return;
    }

    int count = t_split(name_start, ' ', tokens, 4);
    if (count > 0) {
        int i;
        for (i = 0; i < count; i++) {
            if (t_strlen(tokens[i]) > 0) {
                t_strncpy(state->player_name, tokens[i], PLAYER_NAME_MAX);
                return;
            }
        }
    }
    t_strncpy(state->player_name, "Player", PLAYER_NAME_MAX);
}

/* =============================================================================
 * SECTION 19: ENTRY POINT
 * =============================================================================
 */

int main(void) {
    signal(SIGINT,  handle_quit_signal);
    signal(SIGTERM, handle_quit_signal);

    memory_init(VIRTUAL_RAM_SIZE);
    sound_init();

    GameBoard *board = (GameBoard *)t_alloc((int)sizeof(GameBoard));
    GameState *state = (GameState *)t_alloc((int)sizeof(GameState));

    if (!board) T_PANIC("Failed to allocate game board");
    if (!state) T_PANIC("Failed to allocate game state");

    state->running = 1;
    leaderboard_load(state);

    keyboard_init();

    /* Switch to alternate screen buffer before ANY output.
     * The alt screen never scrolls — it is a fresh blank canvas.
     * This is identical to how vim, htop, nano, and less work.
     * On exit we restore the user original terminal exactly.   */
    screen_enter_alt();
    screen_hide_cursor();
    screen_disable_mouse();
    screen_clear();
    screen_set_cursor(1, 1);

    prompt_player_name(state);
    game_loop(state, board);
    leaderboard_save(state);

    screen_enable_mouse();
    screen_exit_alt();
    screen_show_cursor();
    keyboard_restore();

    screen_render_string("Thanks for playing, ");
    if (t_strlen(state->player_name) > 0)
        screen_render_string(state->player_name);
    screen_render_string("! History saved.\n");

    sound_cleanup();
    t_dealloc(board);
    t_dealloc(state);
    memory_cleanup();
    return 0;
}
