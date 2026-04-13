# Tetris OS — Custom C Engine + Web Frontend

> A Tetris game built from scratch in C with **zero standard library dependencies** for core logic.  
> All gameplay is powered by five hand-written foundational libraries: `memory.c`, `string.c`, `math.c`, `screen.c`, `keyboard.c`.

---

## Repository Layout

```
tetris/
├── include/           # Public headers for the 5 custom libraries
│   ├── keyboard.h     # Input abstraction (terminal + WebSocket)
│   ├── memory.h       # Arena allocator (t_alloc / t_dealloc)
│   ├── screen.h       # Output abstraction (ANSI terminal + WebSocket)
│   ├── t_math.h       # Integer math (t_mul, t_div, t_mod, t_in_bounds)
│   └── t_string.h     # String ops (t_strlen, t_strcmp, t_itoa)
├── src/               # Library implementations + terminal main
│   ├── main.c         # Terminal Tetris entry point
│   ├── keyboard.c     # I/O management — terminal raw input + WS recv
│   ├── screen.c       # Display driver — ANSI rendering + WS send
│   ├── memory.c       # Virtual RAM — arena-based allocation
│   ├── math.c         # Custom integer arithmetic
│   └── string.c       # Custom string operations
├── backend/           # WebSocket game server
│   ├── ws_tetris.c    # Game logic + JSON state emitter
│   └── Makefile
├── ui/                # Vite + React browser client
├── Makefile           # Root build for terminal mode
└── README.md
```

## Rules Compliance (Section 2 & 3)

### Non-Negotiable Rules

| Rule | How it's satisfied |
|---|---|
| **Rule 1**: No `<string.h>`, `<math.h>`, `malloc`/`free` | All replaced by `t_string.h`, `t_math.h`, `memory.h` |
| **Rule 2**: No hard-coded logic | Boundaries use `t_in_bounds()`, scoring uses `t_mul()` |
| **Rule 3**: Only `<stdio.h>` and `<stdlib.h>` allowed | `ws_tetris.c` includes *only* these + the 5 custom headers |

### Five-Library Pipeline

```
keyboard.c → captures input     (terminal: getchar / WebSocket: recv)
    ↓
string.c   → parses actions     (t_strcmp, t_itoa, t_strlen)
    ↓
memory.c   → allocates state    (t_alloc for Game struct)
    ↓
math.c     → computes logic     (t_mul, t_mod, t_div, t_in_bounds, t_max)
    ↓
screen.c   → renders output     (terminal: ANSI escapes / WebSocket: send)
```

### Hardware Abstraction (Rule 3 Exception)

Networking headers (`<sys/socket.h>`, `<arpa/inet.h>`, etc.) appear **only** inside `keyboard.c` and `screen.c` — never in game logic. The socket is treated as a hardware device:

- **`screen.c`** = display driver (wraps `send()`, `socket()`, `bind()`, `listen()`, `accept()`)
- **`keyboard.c`** = keyboard driver (wraps `recv()`, `select()`)

This is the same pattern as `<stdio.h>` wrapping terminal I/O.

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

Open [http://localhost:5173](http://localhost:5173). The UI connects to `ws://localhost:8080` by default. To change, set `VITE_WS_URL` in `ui/.env`.

### Controls

| Key | Action |
|---|---|
| `←` / `→` | Move left / right |
| `↑` | Rotate |
| `↓` | Soft drop |
| `Space` | Hard drop |
| `P` | Pause / resume |
| `R` | Restart |

---

## Notes

- Terminal mode persists high scores in `highscore.txt` (git-ignored).
- WebSocket backend is single-client by design.
- The `Game` struct is dynamically allocated via `t_alloc()` and freed with `t_dealloc()`.
- Both builds compile with zero warnings under `-Wall -Wextra`.
