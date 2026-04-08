# Tetris OS Simulator — Phase 1

## Project Description
A fully playable Tetris game built as a simulated OS running in a Linux terminal.
All five custom libraries (math, string, memory, screen, keyboard) are implemented
from scratch and integrated into a real-time interactive game loop — no standard
`<string.h>`, `<math.h>`, or direct `malloc`/`free` used in game logic.

---

## 7 OS Module Coverage

| Module | Implementation |
|---|---|
| **1. Process Management** | `game_loop()` acts as a round-robin scheduler: Input → Physics → Render each frame |
| **2. Memory Management** | `memory.c` — first-fit free-list allocator over a 1 MB virtual RAM slab; board, state, and pieces are all `t_alloc`'d and `t_dealloc`'d |
| **3. File System** | `score_load()` / `score_save()` persist the high score in `highscore.txt` between sessions using `fgetc`/`fputc` (no `fscanf`/`fprintf`) |
| **4. I/O Management** | `keyboard.c` (raw non-blocking input) + `screen.c` (ANSI escape output via `putchar` — no `printf`) |
| **5. Error Handling** | Every `t_alloc` is NULL-checked; all moves validated via `t_in_bounds()`; game-over triggered cleanly instead of crashing |
| **6. Networking** | HUD shows `[SOLO MODE]` stub; architecture is modular for future multiplayer hook-in |
| **7. User Interface** | Full ANSI colored board, ghost shadow, HUD panel, controls legend, game-over overlay — all via `screen.c` |

---

## Controls

| Key | Action |
|---|---|
| `A` | Move piece left |
| `D` | Move piece right |
| `W` | Rotate piece clockwise (with wall-kick) |
| `S` | Soft drop (hold to fall faster) |
| `Space` | Hard drop (instant place, +2 pts/row) |
| `Q` | Quit game |
| `R` | Retry after game over |

---

## Build & Run

```bash
make clean    # remove object files and binary
make          # compiles all .c files, links into ./tetris_os
./tetris_os   # run the game
```

**Requirements:** GCC, a Linux/macOS terminal with ANSI support (80×26 minimum recommended).

---

## Library Usage Summary

| Library | Functions used in main.c |
|---|---|
| `memory.c` | `memory_init`, `t_alloc`, `t_dealloc`, `memory_cleanup` |
| `t_math.c` | `t_mul`, `t_div`, `t_mod`, `t_in_bounds`, `t_max`, `t_clamp` |
| `t_string.c` | `t_itoa`, `t_strcpy`, `t_strlen` |
| `keyboard.c` | `keyboard_init`, `keyboard_restore`, `keyPressed` |
| `screen.c` | `screen_clear`, `screen_set_cursor`, `screen_render_string`, `screen_render_char`, `screen_set_color`, `screen_reset_color`, `screen_hide_cursor`, `screen_show_cursor` |

---

## Known Issues / Phase 1 Scope
- **No audio**: terminal-only, no sound output.
- **Frame rate**: busy-wait timing (no `usleep`) — may vary slightly between machines.
- **Color**: requires an ANSI-compatible terminal (most modern terminals qualify).
- **Networking**: stub only; multiplayer not implemented in Phase 1.
- **`printf`/`scanf`**: not used anywhere — all output via `putchar` + `screen.c`.
