# Tetris OS — Custom C Engine + Web Frontend

> A Tetris game built from scratch in C with **zero standard library dependencies** for core logic.  
> All gameplay is powered by six hand-written foundational libraries: `memory.c`, `string.c`, `math.c`, `screen.c`, `keyboard.c`, `sound.c`.

**GitHub:** https://github.com/Mehak261124/Tetris  
**Demo Recording:**  
Terminal mode: https://drive.google.com/file/d/1WVm6Uo4LR3qpr2OEpeSnANbXet3-6sYx/view?usp=sharing  
Web UI mode: https://drive.google.com/file/d/1Po07KVLVfixfhahjKLgABWrLFSJOFyVc/view?usp=sharing

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
│   └── sound.c        # Audio — WAV generation + cross-platform playback
├── backend/           # WebSocket game server
│   ├── ws_tetris.c    # Game logic + JSON state emitter
│   └── Makefile
├── ui/                # Vite + React browser client
├── recording/         # Demo recordings (terminal + web modes)
├── Makefile           # Root build for terminal mode
└── README.md
```

---

## Demo

A screen recording demonstrating both modes:
- Terminal mode: https://drive.google.com/file/d/1WVm6Uo4LR3qpr2OEpeSnANbXet3-6sYx/view?usp=sharing
- Web UI mode: https://drive.google.com/file/d/1Po07KVLVfixfhahjKLgABWrLFSJOFyVc/view?usp=sharing

---

## 7 OS Module Coverage

### 1. Process Management
> *Who gets to run, when, and where*

| Component | Implementation |
|-----------|---------------|
| **Game loop scheduler** | Fixed-tick main loop (50ms) in `ws_tetris.c` and `main.c` — decides which "process" (input polling, physics, rendering) runs each frame |
| **Gravity tick counter** | NES-style speed curve: `drop_ticks = max(13 - level, 5)` at levels 1–8, faster above |
| **Lock delay timer** | 500ms timer before auto-lock; resets on move/rotate up to `MAX_LOCK_RESETS = 15` |
| **Signal handling** | `SIGINT`/`SIGTERM` set a `volatile sig_atomic_t` flag; loop exits cleanly each frame so terminal is always restored |
| **DAS/ARR** | Web frontend: Delayed Auto Shift (170ms) + Auto Repeat Rate (50ms) |

### 2. Memory Management
> *Who owns which memory, and how much*

| Component | Implementation |
|-----------|---------------|
| **Arena allocator** | `memory.c` — `memory_init(1048576)` pre-allocates a 1 MB slab; `t_alloc()` / `t_dealloc()` manage sub-allocations with first-fit + coalescing |
| **Dynamic game state** | `Game *game = t_alloc(sizeof(Game))` — entire game state dynamically allocated from virtual RAM |
| **Piece lifecycle** | Each `Piece` is `t_alloc()`'d on spawn and `t_dealloc()`'d on lock — zero direct malloc/free |
| **NULL checks** | Every `t_alloc()` return is NULL-checked; failure triggers `T_PANIC` (secure halt) |

### 3. File System
> *Persistent storage — leaderboard, history*

| Component | Implementation |
|-----------|---------------|
| **Shared leaderboard** | `leaderboard.txt` — **both** terminal and web builds read/write the same file so scores are shared across modes |
| **Unified format** | `score\|level\|lines\|name` per line, sorted descending by score |
| **History persistence** | `leaderboard_load()` / `leaderboard_save()` — scores survive process exit and restart |
| **File I/O** | `fopen/fgetc/fputc/fclose` only — no `fprintf`/`fscanf`; parsing via `t_split()` from `string.c` |
| **Edge cases** | Missing file → start empty; malformed lines → skipped; write failure → silent, game continues |

### 4. I/O Management
> *How the system talks to the world — screen, keyboard, speaker*

| Component | Implementation |
|-----------|---------------|
| **Terminal input** | `keyboard.c` — `keyPressed()` non-blocking raw mode, `readLine()` blocking name entry |
| **Terminal output** | `screen.c` — ANSI escape sequences for cursor, color, rendering |
| **WebSocket input** | `keyboard.c` — `keyboard_poll_ws()` (select), `keyboard_recv_ws()` (RFC 6455 frame decode) |
| **WebSocket output** | `screen.c` — `screen_send_ws()` sends WebSocket text frames with JSON game state |
| **Audio — macOS** | `sound.c` — WAV generated via integer arithmetic, played via `afplay` |
| **Audio — Linux** | `sound.c` — same WAV, played via `aplay` (ALSA) with `paplay` (PulseAudio) as fallback |
| **Audio — none** | Runtime `access()` check; if no player found, game runs silently — no crash, no zombie processes |
| **Web audio** | `App.jsx` — Web Audio API oscillators mirror all terminal sound effects and the Korobeiniki melody |

### 5. Error Handling & Security
> *What happens when things go wrong*

| Error | Handler |
|-------|---------|
| Division by zero | `t_div()` / `t_mod()` return 0 safely |
| Memory OOM | `t_alloc()` returns NULL; caller checks before use |
| Board out-of-bounds | `t_in_bounds()` guards every board read and write |
| Buffer overflow | `t_strncpy()` enforces max-length on all copies |
| Double-free | Block-list walk detects and ignores silently |
| Invalid pointer free | Range check + ownership walk rejects it |
| `SIGINT` / `SIGTERM` | Flag set → loop exits → `keyboard_restore()` runs → terminal always left clean |
| `SIGWINCH` (resize) | Flag set → full redraw on next frame |
| Terminal too small | Warning shown; game waits until resized |
| Client disconnect | Backend re-accepts without restarting |
| Malformed WebSocket | Frame silently dropped, loop continues |
| WS handshake failure | Retry with next client |
| Audio player missing | Detected at runtime; game runs silently |
| Secure halt | `screen_panic()` restores cursor + raw mode, prints `[KERNEL PANIC]` to stderr, then exits |

### 6. Networking
> *How this computer talks to another computer*

| Component | Implementation |
|-----------|---------------|
| **WebSocket server** | `screen.c` — `screen_server_listen()` binds + listens, `screen_server_accept()` accepts clients |
| **Persistent server** | Server socket stays open across disconnects — survives page refresh, no restart needed |
| **RFC 6455 compliance** | Full SHA-1 handshake (no OpenSSL), masked frame decode, FIN bit, close frame |
| **JSON state protocol** | Full game state serialised every dirty tick using `t_itoa` + manual string building — no `sprintf` |
| **Auto-reconnect** | React frontend retries with 1.5s backoff on disconnect |
| **Hardware abstraction** | All socket headers (`sys/socket.h`, `arpa/inet.h`) confined to `screen.c` and `keyboard.c` — zero in game logic |

### 7. User Interface
> *How does a user interact with the game*

| Component | Implementation |
|-----------|---------------|
| **Terminal UI** | ANSI-colored 10×20 board, HUD panel, ghost piece, next/hold piece preview, combo display, game-over overlay |
| **Web UI** | React + Vite — neon/CRT aesthetic, 4 themes (Neon, Retro, Mono, Pastel), mobile touch controls |
| **Start screen** | Player name entry with leaderboard preview |
| **Game-over screen** | Stats summary, top-5 leaderboard, "Play Again" / "New Game" options |
| **Hold piece** | Press `C` to stash — dimmed when already used this turn |
| **Ghost piece** | Dashed outline showing landing position |
| **Sound toggle** | Mute/unmute SFX and music independently |
| **Leaderboard tab** | Top-10 scores with current player highlighted |
| **Mobile responsive** | Touch controls, stacked layout on narrow screens |
| **Visual effects** | CRT scanlines, screen shake on line clear, score flash, high-score glow, level-up animation |

---

## Rules Compliance

| Rule | How it is satisfied |
|------|---------------------|
| **Rule 1** — No `<string.h>`, `<math.h>`, `malloc`/`free` | All replaced by `t_string.h`, `t_math.h`, `memory.h` |
| **Rule 2** — No hard-coded logic | Boundaries use `t_in_bounds()`, scoring uses `t_mul()`, levels use `t_div()` |
| **Rule 3** — HAL exception only | POSIX socket headers appear only in `screen.c` and `keyboard.c` |

### Six-Library Pipeline

```
keyboard.c → captures input     (getchar raw / WebSocket recv + frame decode)
    ↓
string.c   → parses actions     (t_strcmp, t_split, t_itoa, t_strlen, t_strncpy)
    ↓
memory.c   → allocates state    (t_alloc for Game struct + every Piece)
    ↓
math.c     → computes logic     (t_mul, t_mod, t_div, t_in_bounds, t_max)
    ↓
screen.c   → renders output     (ANSI escapes / WebSocket text frames)
    ↓
sound.c    → audio feedback     (WAV integer generation / fork+exec platform player)
```

---

## Gameplay Features

| Feature | Terminal | Web |
|---------|:-------:|:---:|
| 7 tetrominoes | ✅ | ✅ |
| Ghost piece preview | ✅ | ✅ |
| Soft drop / Hard drop | ✅ | ✅ |
| Hold piece | ✅ | ✅ |
| 7-bag randomizer | ✅ | ✅ |
| Lock delay (500ms + 15 resets) | ✅ | ✅ |
| Combo system | ✅ | ✅ |
| Level progression | ✅ | ✅ |
| NES-style speed curve | ✅ | ✅ |
| Pause / Resume | ✅ | ✅ |
| Player name entry | ✅ | ✅ |
| Shared leaderboard file | ✅ | ✅ |
| Sound effects | ✅ | ✅ |
| Background music | ✅ | ✅ |
| Theme switcher | 4 block styles | 4 color themes |
| SRS wall kicks | Basic (1-test) | Full (5-test) |
| DAS / ARR | — | ✅ |
| Mobile touch controls | — | ✅ |
| Auto-reconnect on refresh | — | ✅ |
| Top-N leaderboard | Top 5 | Top 10 |

---

## Prerequisites

- C compiler (`gcc` recommended)
- `make`
- Node.js 18+ and npm (for web UI)
- **Linux audio** (optional — game runs silently if absent):
  ```bash
  sudo apt install alsa-utils        # aplay (ALSA)
  sudo apt install pulseaudio-utils  # paplay (PulseAudio fallback)
  ```
- **macOS audio**: `afplay` is built-in, nothing to install.

---

## Option 1: Terminal Tetris

```bash
make clean && make
./tetris_os
```

### Controls

| Key | Action |
|-----|--------|
| `A` / `←` | Move left |
| `D` / `→` | Move right |
| `W` / `↑` | Rotate |
| `S` / `↓` | Soft drop |
| `Space` | Hard drop |
| `C` | Hold piece |
| `T` | Cycle block theme |
| `P` | Pause / resume |
| `Q` | Quit |
| `R` | Retry (after game over) |

---

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
|-----|--------|
| `←` / `→` | Move left / right |
| `↑` | Rotate (SRS wall kicks) |
| `↓` | Soft drop |
| `Space` | Hard drop |
| `C` / `Shift` | Hold piece |
| `P` | Pause / resume |
| `R` | Restart |

---

## Known Issues

- **iTerm2 resize artifacts** — caused by "Save lines to scrollback in alternate screen mode" being enabled. Fixed by: unchecking that setting in iTerm2 → Settings → Profiles → Terminal, plus `\033[3J` added to `screen_clear()` to erase saved lines, and 80ms SIGWINCH debounce added to prevent partial frames during drag.
- **T-spin detection not implemented** — the `last_was_rotate` flag is tracked but bonus scoring is not applied.
- **Terminal leaderboard is top 5; web is top 10** — both share the same file; terminal silently ignores entries 6–10.
- **Background music has a small gap between repeats** in the web client due to the `setTimeout`-based sequencer.
- **No DAS/ARR in terminal mode** — key repeat speed depends on the OS keyboard repeat rate.
- **Linux audio requires an external package** — `aplay` or `paplay` must be installed; game degrades to silence if absent.

---

## Error Handling Summary

| Scenario | Behaviour |
|----------|-----------|
| `t_div(x, 0)` or `t_mod(x, 0)` | Returns 0 — never crashes |
| `t_alloc()` returns NULL | Caller checks; fatal paths call `T_PANIC` |
| Out-of-bounds board access | `t_in_bounds()` returns "filled" — piece rejected |
| Buffer overflow on name entry | `t_strncpy()` truncates at `PLAYER_NAME_MAX` |
| `SIGINT` / `SIGTERM` | Terminal restored before exit — shell left clean |
| Terminal resized (`SIGWINCH`) | 80ms debounce, then single clean full redraw |
| WebSocket client disconnects | Server loops back to `accept()` — no restart |
| Malformed WebSocket frame | Silently dropped — game loop continues |
| Audio player not found | `access()` check at startup; silent fallback |

---

## Notes

- Both terminal and web builds write to the **same `leaderboard.txt`** in the same `score|level|lines|name` format.
- The backend survives page refresh — no need to restart `./tetris_ws`.
- Both builds compile with **zero warnings** under `gcc -Wall -Wextra -O2`.
- Signal handlers are installed **before** `keyboard_init()` so even a crash during startup leaves the terminal clean.
