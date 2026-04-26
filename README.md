# Tetris OS — Custom C Engine + Web Frontend

> A Tetris game built from scratch in C with **zero standard library dependencies** for core logic.  
> All gameplay is powered by six hand-written foundational libraries: `memory.c`, `string.c`, `math.c`, `screen.c`, `keyboard.c`, `sound.c`.

---

## Repository Layout

```
tetris/
├── include/           # Public headers for the custom libraries
│   ├── keyboard.h     # Input abstraction (terminal + WebSocket)
│   ├── memory.h       # Arena allocator (t_alloc / t_dealloc)
│   ├── screen.h       # Output abstraction (ANSI terminal + WebSocket)
│   ├── sound.h        # Audio output (WAV generation + playback)
│   ├── t_math.h       # Integer math (t_mul, t_div, t_mod, t_in_bounds)
│   └── t_string.h     # String ops (t_strlen, t_strcmp, t_itoa)
├── src/               # Library implementations + terminal main
│   ├── main.c         # Terminal Tetris entry point
│   ├── keyboard.c     # I/O management — terminal raw input + WS recv
│   ├── screen.c       # Display driver — ANSI rendering + WS send
│   ├── memory.c       # Virtual RAM — arena-based allocation
│   ├── math.c         # Custom integer arithmetic (shift-and-add)
│   ├── string.c       # Custom string operations
│   └── sound.c        # Audio — WAV generation + playback
├── backend/           # WebSocket game server
│   ├── ws_tetris.c    # Game logic + JSON state emitter
│   └── Makefile
├── ui/                # Vite + React browser client
├── Makefile           # Root build for terminal mode
└── README.md
```

---

## 7 OS Module Coverage

### 1. Process Management
> *Who gets to run, when, and where*

| Component | Implementation |
|-----------|---------------|
| **Game loop scheduler** | Fixed-tick main loop (50ms) in `ws_tetris.c` and `main.c` — decides which "process" (input polling, physics, rendering) runs each frame |
| **Gravity tick counter** | Level-dependent drop speed: `drop_ticks = t_max(14 - level, 2)` |
| **Lock delay timer** | `LOCK_DELAY_TICKS = 10` — piece sits on ground 500ms before auto-locking; resets on move/rotate (up to `MAX_LOCK_RESETS = 15`) |
| **DAS/ARR** | Frontend implements Delayed Auto Shift (170ms) + Auto Repeat Rate (50ms) for smooth key repeat |

### 2. Memory Management
> *Who owns which memory, and how much*

| Component | Implementation |
|-----------|---------------|
| **Arena allocator** | `memory.c` — `memory_init(65536)` pre-allocates a contiguous block; `t_alloc()` / `t_dealloc()` manage sub-allocations |
| **Dynamic Game state** | `Game *game = t_alloc(sizeof(Game))` — entire game state dynamically allocated |
| **Piece lifecycle** | Terminal mode: each `Piece` is `t_alloc()`'d on spawn and `t_dealloc()`'d on lock |
| **NULL checks** | Every `t_alloc()` return is NULL-checked with graceful error handling |

### 3. File System
> *Persistent storage — scores, leaderboard, history*

| Component | Implementation |
|-----------|---------------|
| **High score persistence** | `highscore.txt` — terminal mode saves/loads best score across sessions |
| **Leaderboard** | `leaderboard.txt` — top 10 entries stored as `score\|level\|lines\|name` per line, sorted descending |
| **Load/Save cycle** | `leaderboard_load()` on startup, `leaderboard_save()` after each insertion |
| **File I/O** | Uses `<stdio.h>` `fopen/fgetc/fputc/fclose` only — no `fprintf/fscanf` |

### 4. I/O Management
> *How the system talks to the world — screen, keyboard, speaker*

| Component | Implementation |
|-----------|---------------|
| **Terminal input** | `keyboard.c` — `keyPressed()` non-blocking raw mode, `readLine()` blocking name entry |
| **Terminal output** | `screen.c` — ANSI escape sequences for cursor, color, rendering |
| **WebSocket input** | `keyboard.c` — `keyboard_poll_ws()` (select), `keyboard_recv_ws()` (WebSocket frame decode) |
| **WebSocket output** | `screen.c` — `screen_send_ws()` sends WebSocket text frames with JSON game state |
| **Audio output** | `sound.c` — generates square-wave WAV files via integer arithmetic (no `<math.h>`), plays via `afplay` (macOS) |
| **Web Audio** | `App.jsx` — Web Audio API oscillators mirror terminal sound effects |

### 5. Error Handling & Security
> *What happens when things go wrong*

| Component | Implementation |
|-----------|---------------|
| **Boundary checks** | `t_in_bounds()` used for every board access, piece movement, and collision |
| **Division by zero** | `t_div()` and `t_mod()` return 0 if divisor is 0 |
| **Memory safety** | Every `t_alloc()` is NULL-checked; `t_dealloc()` validates pointers |
| **Input validation** | JSON parser (`parse_action`, `parse_name`) validates format before processing |
| **WebSocket handshake** | SHA-1 + Base64 handshake validates the Sec-WebSocket-Key header |
| **Graceful disconnect** | Backend survives page refresh — outer loop re-accepts clients |
| **Buffer overflow guard** | All string copies use `t_strncpy()` with max-length parameter |

### 6. Networking
> *How this computer talks to another computer*

| Component | Implementation |
|-----------|---------------|
| **WebSocket server** | `screen.c` — `screen_server_listen()` binds + listens, `screen_server_accept()` accepts clients |
| **Persistent server** | Server socket stays open across client disconnects — supports page refresh and new connections |
| **Protocol handling** | Full RFC 6455 WebSocket: SHA-1 handshake, frame encode/decode with masking |
| **JSON state protocol** | Game state serialized as JSON every frame: board, pieces, score, leaderboard |
| **Auto-reconnect** | Frontend WebSocket client auto-reconnects with 1.5s backoff on disconnect |
| **Hardware abstraction** | All socket/POSIX headers confined to `screen.c` and `keyboard.c` — zero networking code in game logic |

### 7. User Interface
> *How does a user interact with the game*

| Component | Implementation |
|-----------|---------------|
| **Terminal UI** | ANSI-colored board, HUD panel, ghost piece, next piece preview, game-over overlay |
| **Web UI** | React + Vite — neon/CRT aesthetic, responsive layout, mobile touch controls |
| **Start screen** | Player name entry with leaderboard preview |
| **Game-over screen** | Stats summary, "Play Again" (same player) / "New Game" (new player), leaderboard link |
| **Hold piece** | Press C/Shift to stash current piece — visual preview with dimmed state when used |
| **Ghost piece** | Transparent outline showing where the piece will land |
| **Sound toggle** | Mute/unmute button in side panel |
| **Leaderboard tab** | Top-10 scores with player highlighting |
| **Mobile responsive** | Touch controls, stacked layout on narrow screens |
| **Visual effects** | CRT scanlines, screen shake, score flash, line-clear animation, high-score glow |

---

## Rules Compliance (Section 2 & 3)

### Non-Negotiable Rules

| Rule | How it's satisfied |
|---|---|
| **Rule 1**: No `<string.h>`, `<math.h>`, `malloc`/`free` | All replaced by `t_string.h`, `t_math.h`, `memory.h` |
| **Rule 2**: No hard-coded logic | Boundaries use `t_in_bounds()`, scoring uses `t_mul()` |
| **Rule 3**: Only `<stdio.h>` and `<stdlib.h>` allowed | `ws_tetris.c` includes *only* these + the custom headers |

### Six-Library Pipeline

```
keyboard.c → captures input     (terminal: getchar / WebSocket: recv)
    ↓
string.c   → parses actions     (t_strcmp, t_itoa, t_strlen, t_strncpy)
    ↓
memory.c   → allocates state    (t_alloc for Game struct, Piece lifecycle)
    ↓
math.c     → computes logic     (t_mul, t_mod, t_div, t_in_bounds, t_max)
    ↓
screen.c   → renders output     (terminal: ANSI / WebSocket: send)
    ↓
sound.c    → audio feedback     (WAV generation / Web Audio oscillators)
```

### Hardware Abstraction (Rule 3 Exception)

Networking headers (`<sys/socket.h>`, `<arpa/inet.h>`, etc.) appear **only** inside `keyboard.c` and `screen.c` — never in game logic. The socket is treated as a hardware device:

- **`screen.c`** = display driver (wraps `send()`, `socket()`, `bind()`, `listen()`, `accept()`)
- **`keyboard.c`** = keyboard driver (wraps `recv()`, `select()`)

---

## Gameplay Features

| Feature | Terminal | Web |
|---------|:-------:|:---:|
| 7 tetrominoes with SRS rotation | ✅ | ✅ |
| **SRS Wall Kicks** (5-test per rotation) | — | ✅ |
| **7-Bag Randomizer** (fair piece distribution) | — | ✅ |
| **Hold Piece** (C/Shift to swap) | — | ✅ |
| **Lock Delay** (500ms + 15 reset limit) | — | ✅ |
| **DAS/ARR** (170ms delay, 50ms repeat) | — | ✅ |
| Ghost piece preview | ✅ | ✅ |
| Soft drop / Hard drop | ✅ | ✅ |
| Line clear scoring (single/double/triple/tetris) | ✅ | ✅ |
| Level progression | ✅ | ✅ |
| Pause / Resume | ✅ | ✅ |
| Player name entry | ✅ | ✅ |
| High score persistence | ✅ | ✅ |
| Top-10 leaderboard | — | ✅ |
| Sound effects | ✅ | ✅ |
| Background music | ✅ | — |
| Play Again / New Game | ✅ | ✅ |
| Mobile touch controls | — | ✅ |
| Auto-reconnect on refresh | — | ✅ |

---

## Prerequisites

- C compiler (`gcc` recommended)
- `make`
- Node.js 18+ and npm (for web UI)

## Option 1: Terminal Tetris

```bash
make clean && make
./tetris_os
```

### Controls

| Key | Action |
|---|---|
| `A` / `D` | Move left / right |
| `W` | Rotate |
| `S` | Soft drop |
| `Space` | Hard drop |
| `Q` | Quit |
| `R` | Retry (after game over) |

## Option 2: Web Tetris (Backend + UI)

**Terminal 1 — Start backend:**

```bash
cd backend
make clean && make
./tetris_ws
```

**Terminal 2 — Start frontend:**

```bash
cd ui
npm install
npm run dev
```

Open [http://localhost:5173](http://localhost:5173). The UI connects to `ws://localhost:8080` by default.

### Controls

| Key | Action |
|---|---|
| `←` / `→` | Move left / right |
| `↑` | Rotate (SRS wall kicks) |
| `↓` | Soft drop |
| `Space` | Hard drop |
| `C` / `Shift` | Hold piece |
| `P` | Pause / resume |
| `R` | Restart |

---

## Error Handling Summary

- **Division by zero**: `t_div()` / `t_mod()` return 0 safely
- **Memory allocation failure**: All `t_alloc()` calls NULL-checked
- **Out-of-bounds access**: `t_in_bounds()` guards all board/piece operations
- **Buffer overflow**: `t_strncpy()` limits all string copies
- **Client disconnect**: Backend re-accepts without restarting
- **Invalid WebSocket messages**: Silently dropped, no crash
- **Malformed JSON**: Parser returns 0, action skipped

## Notes

- Terminal mode persists high scores in `highscore.txt`.
- WebSocket backend persists leaderboard in `leaderboard.txt`.
- Backend survives page refresh — no need to restart.
- The `Game` struct is dynamically allocated via `t_alloc()` and freed with `t_dealloc()`.
- Both builds compile with **zero warnings** under `-Wall -Wextra`.
