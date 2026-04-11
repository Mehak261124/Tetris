# Tetris Project (C + Web)

This repository contains two playable Tetris experiences:

1. A terminal-based game written in C (`tetris_os`).
2. A browser-based game with a C WebSocket backend and a React frontend (`backend` + `ui`).

## Repository Layout

- `src/`, `include/`, `Makefile`: terminal Tetris and custom C utility modules.
- `backend/`: standalone C WebSocket server (`tetris_ws`) that emits game state JSON.
- `ui/`: Vite + React client that renders the board and sends player actions.

## Prerequisites

- C compiler (`gcc` recommended)
- `make`
- Node.js 18+ and npm (for `ui`)

## Option 1: Run Terminal Tetris

```bash
make clean
make
./tetris_os
```

### Terminal Controls

- `A`: move left
- `D`: move right
- `W`: rotate
- `S`: soft drop
- `Space`: hard drop
- `Q`: quit
- `R`: retry (after game over)

## Option 2: Run Web Tetris (Backend + UI)

In one terminal:

```bash
cd backend
make clean
make
./tetris_ws
```

In a second terminal:

```bash
cd ui
npm install
npm run dev
```

Open `http://localhost:5173`.

The UI connects to `ws://localhost:8080` by default.  
To use another backend URL, set `VITE_WS_URL` in `ui/.env`.

### Web Controls

- `Arrow Left/Right`: move
- `Arrow Up`: rotate
- `Arrow Down`: soft drop
- `Space`: hard drop
- `P`: pause/resume
- `R`: restart

## Notes

- Terminal mode persists score in `highscore.txt` (ignored by git).
- WebSocket backend is single-client by design.
- No automated tests are currently configured in this repository.
