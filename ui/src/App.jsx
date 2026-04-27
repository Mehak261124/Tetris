import { useCallback, useEffect, useLayoutEffect, useMemo, useReducer, useRef, useState } from "react";

/* =============================================================================
 * CONSTANTS
 * ============================================================================= */
const BOARD_ROWS = 20;
const BOARD_COLS = 10;
const PREVIEW_SIZE = 4;

const PIECE_INDEX = {
  I: 1, O: 2, T: 3, S: 4, Z: 5, J: 6, L: 7,
};

const WS_URL = import.meta.env.VITE_WS_URL || "ws://localhost:8080";

/* DAS (Delayed Auto Shift) / ARR (Auto Repeat Rate) — in milliseconds */
const DAS_DELAY = 170;
const ARR_RATE  = 50;
const REPEATABLE_ACTIONS = new Set(["move_left", "move_right", "soft_drop"]);

/* Theme definitions — CSS custom property overrides */
const THEMES = {
  neon:   { label: "Neon",   className: "" },
  retro:  { label: "Retro",  className: "theme-retro" },
  mono:   { label: "Mono",   className: "theme-mono" },
  pastel: { label: "Pastel", className: "theme-pastel" },
};

/* =============================================================================
 * WEB AUDIO — Sound Effects matching terminal sound.c
 * ============================================================================= */

let audioCtx = null;

function getAudioCtx() {
  if (!audioCtx) {
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  }
  return audioCtx;
}

function playTone(freq, duration, type = "square", volume = 0.08) {
  try {
    const ctx = getAudioCtx();
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = type;
    osc.frequency.setValueAtTime(freq, ctx.currentTime);
    gain.gain.setValueAtTime(volume, ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + duration);
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start(ctx.currentTime);
    osc.stop(ctx.currentTime + duration);
  } catch (e) { /* audio not available */ }
}

const SFX = {
  move: () => playTone(220, 0.06, "square", 0.05),
  rotate: () => playTone(440, 0.08, "square", 0.06),
  drop: () => {
    playTone(110, 0.12, "square", 0.1);
    setTimeout(() => playTone(80, 0.1, "square", 0.08), 40);
  },
  hold: () => playTone(330, 0.1, "triangle", 0.06),
  clear: () => {
    [523, 659, 784, 1047].forEach((f, i) =>
      setTimeout(() => playTone(f, 0.12, "square", 0.07), i * 60)
    );
  },
  levelUp: () => {
    [392, 494, 587, 784].forEach((f, i) =>
      setTimeout(() => playTone(f, 0.15, "triangle", 0.08), i * 80)
    );
  },
  gameover: () => {
    [440, 370, 311, 262].forEach((f, i) =>
      setTimeout(() => playTone(f, 0.25, "sawtooth", 0.06), i * 180)
    );
  },
};

/* =============================================================================
 * BACKGROUND MUSIC — Korobeiniki melody via Web Audio oscillators
 * ============================================================================= */
const MELODY = [
  [659,400],[494,200],[523,200],[587,400],[523,200],[494,200],
  [440,400],[440,200],[523,200],[659,400],[587,200],[523,200],
  [494,400],[494,200],[523,200],[587,400],[659,400],
  [523,400],[440,400],[440,400],[0,400],
  [587,400],[698,200],[880,400],[784,200],[698,200],
  [659,400],[523,200],[659,400],[587,200],[523,200],
  [494,400],[494,200],[523,200],[587,400],[659,400],
  [523,400],[440,400],[440,400],[0,400],
];

class BGMusic {
  constructor() {
    this._playing = false;
    this._timer = null;
    this._idx = 0;
  }
  start() {
    if (this._playing) return;
    this._playing = true;
    this._idx = 0;
    this._tick();
  }
  stop() {
    this._playing = false;
    if (this._timer) { clearTimeout(this._timer); this._timer = null; }
  }
  _tick() {
    if (!this._playing) return;
    const [freq, dur] = MELODY[this._idx];
    if (freq > 0) playTone(freq, dur / 1200, "square", 0.03);
    this._idx = (this._idx + 1) % MELODY.length;
    this._timer = setTimeout(() => this._tick(), dur);
  }
}
const bgm = new BGMusic();

/* =============================================================================
 * GRID HELPERS
 * ============================================================================= */

const emptyBoard = () =>
  Array.from({ length: BOARD_ROWS }, () => Array(BOARD_COLS).fill(0));

const normalizeBoard = (board) => {
  if (!Array.isArray(board) || board.length !== BOARD_ROWS) return emptyBoard();
  return board.map((row) => {
    if (!Array.isArray(row) || row.length !== BOARD_COLS) {
      return Array(BOARD_COLS).fill(0);
    }
    return row.map((cell) => (Number.isFinite(cell) ? cell : 0));
  });
};

const pieceTypeToIndex = (piece) => {
  if (!piece || piece.type == null) return 0;
  if (typeof piece.type === "number") return piece.type;
  if (typeof piece.type === "string") {
    return PIECE_INDEX[piece.type.toUpperCase()] || 0;
  }
  return 0;
};

const overlayPiece = (grid, piece, kind) => {
  if (!piece || !Array.isArray(piece.shape)) return;
  const typeIndex = pieceTypeToIndex(piece);
  const shape = piece.shape;
  for (let r = 0; r < shape.length; r += 1) {
    for (let c = 0; c < shape[r].length; c += 1) {
      if (!shape[r][c]) continue;
      const x = piece.x + c;
      const y = piece.y + r;
      if (y < 0 || y >= BOARD_ROWS || x < 0 || x >= BOARD_COLS) continue;
      const cell = grid[y][x];
      if (kind === "active") {
        cell.active = true;
        cell.activeType = typeIndex;
      } else if (!cell.active) {
        cell.ghost = true;
        cell.ghostType = typeIndex;
      }
    }
  }
};

const buildRenderGrid = (board, currentPiece, ghostPiece) => {
  const base = normalizeBoard(board).map((row) =>
    row.map((value) => ({
      type: value,
      active: false,
      ghost: false,
      activeType: 0,
      ghostType: 0,
    }))
  );
  overlayPiece(base, ghostPiece, "ghost");
  overlayPiece(base, currentPiece, "active");
  return base;
};

/* =============================================================================
 * REDUCER
 * ============================================================================= */

const initialState = {
  board: emptyBoard(),
  currentPiece: null,
  ghostPiece: null,
  nextPiece: null,
  heldPiece: null,
  holdUsed: false,
  score: 0,
  highScore: 0,
  level: 1,
  lines: 0,
  combo: 0,
  gameState: "playing",
  wsStatus: "connecting",
  playerName: "",
  leaderboard: [],
};

const reducer = (state, action) => {
  switch (action.type) {
    case "WS_STATUS":
      return { ...state, wsStatus: action.status };
    case "UPDATE_STATE":
      return {
        ...state,
        board: normalizeBoard(action.payload.board),
        currentPiece: action.payload.currentPiece || null,
        ghostPiece: action.payload.ghostPiece || null,
        nextPiece: action.payload.nextPiece || null,
        heldPiece: action.payload.heldPiece !== undefined ? action.payload.heldPiece : state.heldPiece,
        holdUsed: action.payload.holdUsed !== undefined ? action.payload.holdUsed : state.holdUsed,
        score: Number.isFinite(action.payload.score) ? action.payload.score : 0,
        highScore: Number.isFinite(action.payload.highScore) ? action.payload.highScore : 0,
        level: Number.isFinite(action.payload.level) ? action.payload.level : 1,
        lines: Number.isFinite(action.payload.lines) ? action.payload.lines : 0,
        combo: Number.isFinite(action.payload.combo) ? action.payload.combo : 0,
        gameState: action.payload.state || "playing",
        playerName: action.payload.playerName || state.playerName || "Player",
        leaderboard: Array.isArray(action.payload.leaderboard) ? action.payload.leaderboard : state.leaderboard,
      };
    case "SET_NAME":
      return { ...state, playerName: action.name };
    default:
      return state;
  }
};

const actionForKey = (event) => {
  const code = event.code;
  const key = event.key;
  if (code === "ArrowLeft"  || key === "ArrowLeft"  || key === "Left")  return "move_left";
  if (code === "ArrowRight" || key === "ArrowRight" || key === "Right") return "move_right";
  if (code === "ArrowUp"    || key === "ArrowUp"    || key === "Up")    return "rotate";
  if (code === "ArrowDown"  || key === "ArrowDown"  || key === "Down")  return "soft_drop";
  if (code === "Space" || key === " " || key === "Spacebar") return "hard_drop";
  if (code === "KeyP" || key === "p" || key === "P") return "pause";
  if (code === "KeyR" || key === "r" || key === "R") return "restart";
  if (code === "KeyC" || key === "c" || key === "C" ||
      code === "ShiftLeft" || code === "ShiftRight" ||
      key === "Shift") return "hold";
  return null;
};

/* =============================================================================
 * MAIN APP
 * ============================================================================= */

export default function App() {
  const [state, dispatch] = useReducer(reducer, initialState);
  const wsRef            = useRef(null);
  const reconnectTimer   = useRef(null);

  /* --- animation state --- */
  const [scoreFlash,   setScoreFlash]   = useState(false);
  const [lineFlash,    setLineFlash]    = useState(false);
  const [boardShake,   setBoardShake]   = useState(false);
  const [levelUpFlash, setLevelUpFlash] = useState(false);

  /* --- prev-value trackers --- */
  const prevLines     = useRef(state.lines);
  const prevScore     = useRef(state.score);
  const prevGameState = useRef(state.gameState);
  const prevLevel     = useRef(state.level);

  /* --- UI state --- */
  const [showStartScreen, setShowStartScreen] = useState(true);
  const [nameInput,       setNameInput]       = useState("");
  const [soundEnabled,    setSoundEnabled]    = useState(true);
  const [musicEnabled,    setMusicEnabled]    = useState(false);
  const [activeTab,       setActiveTab]       = useState("controls");
  const [theme,           setTheme]           = useState("neon");

  /* --- refs --- */
  const nameInputRef = useRef(null);
  const dasRef       = useRef({ key: null, dasTimer: null, arrTimer: null });

  /* ── KEY FIX: measure left-column header height and apply to spacer ──────── */
  const headerRef = useRef(null);   /* wraps game-title + player-bar          */
  const spacerRef = useRef(null);   /* the invisible spacer in side-panel     */

  useLayoutEffect(() => {
    const sync = () => {
      if (headerRef.current && spacerRef.current) {
        const h = headerRef.current.getBoundingClientRect().height;
        spacerRef.current.style.height = `${h}px`;
      }
    };
    sync();
    window.addEventListener("resize", sync);
    return () => window.removeEventListener("resize", sync);
  }, [showStartScreen]); /* re-run when start screen hides so refs are mounted */

  /* ── grid ── */
  const grid = useMemo(
    () => buildRenderGrid(state.board, state.currentPiece, state.ghostPiece),
    [state.board, state.currentPiece, state.ghostPiece]
  );

  /* ── sound effects ── */
  useEffect(() => {
    if (!soundEnabled || showStartScreen) return;
    prevScore.current = state.score;
  }, [state.score, state.lines, soundEnabled, showStartScreen]);

  useEffect(() => {
    if (!soundEnabled || showStartScreen) return;
    if (state.lines > prevLines.current) SFX.clear();
    prevLines.current = state.lines;
  }, [state.lines, soundEnabled, showStartScreen]);

  useEffect(() => {
    if (!soundEnabled || showStartScreen) return;
    if (state.gameState === "gameover" && prevGameState.current !== "gameover") SFX.gameover();
    prevGameState.current = state.gameState;
  }, [state.gameState, soundEnabled, showStartScreen]);

  /* ── level-up ── */
  useEffect(() => {
    if (state.level > prevLevel.current && prevLevel.current > 0) {
      if (soundEnabled) SFX.levelUp();
      setLevelUpFlash(true);
      const t = setTimeout(() => setLevelUpFlash(false), 600);
      return () => clearTimeout(t);
    }
    prevLevel.current = state.level;
  }, [state.level, soundEnabled]);

  /* ── background music ── */
  useEffect(() => {
    if (musicEnabled && !showStartScreen && state.gameState === "playing") {
      bgm.start();
    } else {
      bgm.stop();
    }
    return () => bgm.stop();
  }, [musicEnabled, showStartScreen, state.gameState]);

  /* ── score flash ── */
  useEffect(() => {
    if (!Number.isFinite(state.score)) return;
    setScoreFlash(true);
    const t = setTimeout(() => setScoreFlash(false), 200);
    return () => clearTimeout(t);
  }, [state.score]);

  /* ── line-clear flash + shake ── */
  useEffect(() => {
    if (!Number.isFinite(state.lines)) return;
    setLineFlash(true);
    const t = setTimeout(() => setLineFlash(false), 160);
    if (state.lines > prevLines.current) {
      setBoardShake(true);
      const t2 = setTimeout(() => setBoardShake(false), 250);
      return () => { clearTimeout(t); clearTimeout(t2); };
    }
    return () => clearTimeout(t);
  }, [state.lines]);

  /* ── WebSocket ── */
  useEffect(() => {
    let active = true;
    const connect = () => {
      if (!active) return;
      dispatch({ type: "WS_STATUS", status: "connecting" });
      const ws = new WebSocket(WS_URL);
      wsRef.current = ws;
      ws.onopen    = () => dispatch({ type: "WS_STATUS", status: "open" });
      ws.onmessage = (event) => {
        try {
          dispatch({ type: "UPDATE_STATE", payload: JSON.parse(event.data) });
        } catch {
          dispatch({ type: "WS_STATUS", status: "error" });
        }
      };
      ws.onclose = () => {
        if (!active) return;
        dispatch({ type: "WS_STATUS", status: "reconnecting" });
        reconnectTimer.current = setTimeout(connect, 1500);
      };
      ws.onerror = () => { ws.close(); };
    };
    connect();
    return () => {
      active = false;
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      if (wsRef.current) wsRef.current.close();
    };
  }, []);

  /* ── sendAction ── */
  const sendAction = useCallback((action, extra = {}) => {
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    ws.send(JSON.stringify({ action, ...extra }));
    if (soundEnabled) {
      if (action === "move_left" || action === "move_right") SFX.move();
      else if (action === "rotate")    SFX.rotate();
      else if (action === "hard_drop") SFX.drop();
      else if (action === "hold")      SFX.hold();
    }
  }, [soundEnabled]);

  /* ── DAS / ARR ── */
  const clearDAS = useCallback(() => {
    const d = dasRef.current;
    if (d.dasTimer) { clearTimeout(d.dasTimer);  d.dasTimer = null; }
    if (d.arrTimer) { clearInterval(d.arrTimer); d.arrTimer = null; }
    d.key = null;
  }, []);

  const startDAS = useCallback((action) => {
    clearDAS();
    const d = dasRef.current;
    d.key = action;
    d.dasTimer = setTimeout(() => {
      d.arrTimer = setInterval(() => sendAction(action), ARR_RATE);
    }, DAS_DELAY);
  }, [sendAction, clearDAS]);

  const handleKeyDown = useCallback((event) => {
    if (showStartScreen) return;
    const action = actionForKey(event);
    if (!action) return;
    if (["ArrowLeft","ArrowRight","ArrowUp","ArrowDown","Space"].includes(event.code)) {
      event.preventDefault();
    }
    if (REPEATABLE_ACTIONS.has(action)) {
      if (dasRef.current.key === action) return;
      sendAction(action);
      startDAS(action);
    } else {
      sendAction(action);
    }
  }, [sendAction, showStartScreen, startDAS]);

  const handleKeyUp = useCallback((event) => {
    const action = actionForKey(event);
    if (action && dasRef.current.key === action) clearDAS();
  }, [clearDAS]);

  useEffect(() => {
    window.addEventListener("keydown", handleKeyDown);
    window.addEventListener("keyup",   handleKeyUp);
    return () => {
      window.removeEventListener("keydown", handleKeyDown);
      window.removeEventListener("keyup",   handleKeyUp);
      clearDAS();
    };
  }, [handleKeyDown, handleKeyUp, clearDAS]);

  /* ── focus name input ── */
  useEffect(() => {
    if (showStartScreen && nameInputRef.current) nameInputRef.current.focus();
  }, [showStartScreen]);

  /* ── start game ── */
  const handleStartGame = useCallback(() => {
    const name = nameInput.trim() || "Player";
    dispatch({ type: "SET_NAME", name });
    sendAction("set_name", { name });
    setShowStartScreen(false);
    try { getAudioCtx().resume(); } catch (e) { /* ok */ }
  }, [nameInput, sendAction]);

  /* ── derived ── */
  const showOverlay    = state.gameState === "gameover";
  const showPaused     = state.gameState === "paused";
  const isNewHighScore = state.score > 0 && state.score >= state.highScore;
  const wsMessage =
    state.wsStatus === "reconnecting" ? "Reconnecting..."  :
    state.wsStatus === "connecting"   ? "Connecting..."    :
    state.wsStatus === "error"        ? "Connection error" : "";

  const boardClasses = [
    "board-shell", "crt",
    lineFlash  ? "lines-flash" : "",
    boardShake ? "board-shake" : "",
  ].filter(Boolean).join(" ");

  /* ====================== START SCREEN ====================== */
  if (showStartScreen) {
    return (
      <div className="game-wrapper">
        <div className="start-screen">
          <div className="start-logo">
            <div className="start-logo-text">TETRIS</div>
            <div className="start-logo-sub">OS SIMULATOR</div>
          </div>
          <div className="start-form">
            <label className="start-label" htmlFor="player-name">
              Enter your name
            </label>
            <input
              ref={nameInputRef}
              id="player-name"
              className="start-input"
              type="text"
              maxLength={19}
              placeholder="Player"
              value={nameInput}
              onChange={(e) => setNameInput(e.target.value)}
              onKeyDown={(e) => { if (e.key === "Enter") handleStartGame(); }}
            />
            <button id="start-button" className="start-btn" onClick={handleStartGame}>
              START GAME
            </button>
          </div>
          {state.leaderboard.length > 0 && (
            <div className="start-leaderboard">
              <div className="panel-title" style={{ marginBottom: "8px" }}>
                🏆 Leaderboard
              </div>
              {state.leaderboard.slice(0, 5).map((entry, i) => (
                <div key={i} className="start-lb-row">
                  <span className="start-lb-rank">
                    {i === 0 ? "🥇" : i === 1 ? "🥈" : i === 2 ? "🥉" : `#${i + 1}`}
                  </span>
                  <span className="start-lb-name">{entry.name}</span>
                  <span className="start-lb-score">{entry.score}</span>
                </div>
              ))}
            </div>
          )}
          {wsMessage && <div className="ws-status" style={{ marginTop: 12 }}>{wsMessage}</div>}
        </div>
      </div>
    );
  }

  /* ====================== MAIN GAME ====================== */
  const themeClass = THEMES[theme]?.className || "";

  return (
    <div className={`game-wrapper ${themeClass}`}>
      <div className="game-layout">

        {/* ── Left: Game Board ── */}
        <div className="game-column">

          {/* Header wrapper — measured by headerRef */}
          <div ref={headerRef} className="game-column-header">
            <div className="game-title">TETRIS</div>
            <div className="player-bar">
              <span className="player-bar-label">Player:</span>{" "}
              <span className="player-bar-name">{state.playerName || "Player"}</span>
            </div>
          </div>

          <div className={boardClasses}>
            <div className="scanlines" />
            <div className="board-grid">
              {grid.map((row, rowIndex) =>
                row.map((cell, colIndex) => {
                  const type      = cell.active ? cell.activeType : cell.type;
                  const ghostType = cell.ghost   ? cell.ghostType  : 0;
                  const dataType  = type || ghostType || 0;
                  return (
                    <div
                      key={`${rowIndex}-${colIndex}`}
                      className={`cell ${cell.active ? "cell-active" : ""}`}
                      data-type={dataType}
                      data-ghost={cell.ghost && !cell.active ? "true" : "false"}
                    />
                  );
                })
              )}
            </div>
            {wsMessage && <div className="ws-status">{wsMessage}</div>}

            {showPaused && (
              <div className="overlay">
                <div className="overlay-card">
                  <div className="overlay-title pause-title">⏸ PAUSED</div>
                  <button className="overlay-button" onClick={() => sendAction("pause")}>
                    Resume
                  </button>
                </div>
              </div>
            )}

            {showOverlay && (
              <div className="overlay">
                <div className="overlay-card gameover-card">
                  <div className="overlay-title">GAME OVER</div>
                  {isNewHighScore && (
                    <div className="new-highscore-badge">🏆 NEW HIGH SCORE!</div>
                  )}
                  <div className="gameover-stats">
                    <div className="gameover-stat">
                      <span className="gameover-stat-label">Player</span>
                      <span className="gameover-stat-value">{state.playerName}</span>
                    </div>
                    <div className="gameover-stat">
                      <span className="gameover-stat-label">Score</span>
                      <span className="gameover-stat-value">{state.score}</span>
                    </div>
                    <div className="gameover-stat">
                      <span className="gameover-stat-label">Level</span>
                      <span className="gameover-stat-value">{state.level}</span>
                    </div>
                    <div className="gameover-stat">
                      <span className="gameover-stat-label">Lines</span>
                      <span className="gameover-stat-value">{state.lines}</span>
                    </div>
                  </div>
                  <div className="gameover-actions">
                    <button className="overlay-button" onClick={() => sendAction("restart")}>
                      Play Again
                    </button>
                    <button
                      className="overlay-button overlay-button-secondary"
                      onClick={() => {
                        sendAction("restart");
                        setShowStartScreen(true);
                        setNameInput("");
                      }}
                    >
                      New Game
                    </button>
                  </div>
                  <button className="overlay-link" onClick={() => setActiveTab("leaderboard")}>
                    🏆 View Leaderboard
                  </button>
                </div>
              </div>
            )}
          </div>

          {/* Mobile Touch Controls */}
          <div className="touch-controls">
            <div className="touch-row">
              <button className="touch-btn" onClick={() => sendAction("move_left")}>◀</button>
              <button className="touch-btn" onClick={() => sendAction("rotate")}>▲</button>
              <button className="touch-btn" onClick={() => sendAction("soft_drop")}>▼</button>
              <button className="touch-btn" onClick={() => sendAction("move_right")}>▶</button>
            </div>
            <div className="touch-row">
              <button className="touch-btn touch-btn-wide" onClick={() => sendAction("hard_drop")}>DROP</button>
              <button className="touch-btn" onClick={() => sendAction("hold")}>⬡</button>
              <button className="touch-btn" onClick={() => sendAction("pause")}>⏸</button>
              <button className="touch-btn" onClick={() => sendAction("restart")}>↻</button>
            </div>
          </div>
        </div>

        {/* ── Right: Side Panel ── */}
        <div className="side-panel">

          {/* Spacer measured by JS — height set dynamically to match headerRef */}
          <div ref={spacerRef} className="side-panel-spacer" />

          {/* Hold + Next */}
          <div className="preview-row">
            <div className={`panel-block panel-compact ${state.holdUsed ? "hold-used" : ""}`}>
              <div className="panel-title">Hold <span className="hold-key-hint">[C]</span></div>
              <HoldPiecePreview heldPiece={state.heldPiece} holdUsed={state.holdUsed} />
            </div>
            <div className="panel-block panel-compact">
              <div className="panel-title">Next</div>
              <NextPiecePreview nextPiece={state.nextPiece} />
            </div>
          </div>

          {/* Stats */}
          <div className="panel-block">
            <div className="stats-grid">
              <div className="stat-item">
                <div className="panel-title">Score</div>
                <div className={`panel-value ${scoreFlash ? "score-flash" : ""}`}>{state.score}</div>
              </div>
              <div className="stat-item">
                <div className="panel-title">Best</div>
                <div className={`panel-value highscore-value ${isNewHighScore ? "highscore-glow" : ""}`}>
                  {state.highScore}
                </div>
              </div>
              <div className="stat-item">
                <div className="panel-title">Level</div>
                <div className={`panel-value ${levelUpFlash ? "level-up-flash" : ""}`}>{state.level}</div>
              </div>
              <div className="stat-item">
                <div className="panel-title">Lines</div>
                <div className="panel-value">{state.lines}</div>
              </div>
            </div>
            {state.combo > 1 && (
              <div className="combo-indicator">🔥 {state.combo}x Combo!</div>
            )}
          </div>

          {/* Sound + Music */}
          <div className="audio-row">
            <button
              id="sound-toggle"
              className={`sound-toggle ${soundEnabled ? "sound-on" : "sound-off"}`}
              onClick={() => setSoundEnabled((v) => !v)}
              title={soundEnabled ? "Mute sounds" : "Unmute sounds"}
            >
              {soundEnabled ? "🔊" : "🔇"} SFX
            </button>
            <button
              className={`sound-toggle ${musicEnabled ? "sound-on" : "sound-off"}`}
              onClick={() => setMusicEnabled((v) => !v)}
              title={musicEnabled ? "Stop music" : "Play music"}
            >
              {musicEnabled ? "🎵" : "⏹"} Music
            </button>
          </div>

          {/* Theme picker */}
          <div className="theme-picker">
            {Object.entries(THEMES).map(([key, t]) => (
              <button
                key={key}
                className={`theme-btn ${theme === key ? "theme-active" : ""}`}
                onClick={() => setTheme(key)}
              >
                {t.label}
              </button>
            ))}
          </div>

          {/* Tab switcher */}
          <div className="tab-switcher">
            <button
              className={`tab-btn ${activeTab === "controls" ? "tab-active" : ""}`}
              onClick={() => setActiveTab("controls")}
            >
              Controls
            </button>
            <button
              className={`tab-btn ${activeTab === "leaderboard" ? "tab-active" : ""}`}
              onClick={() => setActiveTab("leaderboard")}
            >
              🏆 Board
            </button>
          </div>

          {activeTab === "controls" && (
            <div className="panel-block controls-desktop">
              <ul className="controls">
                <li>← → : Move</li>
                <li>↑ : Rotate</li>
                <li>↓ : Soft Drop</li>
                <li>Space: Hard Drop</li>
                <li>C/Shift: Hold</li>
                <li>P: Pause</li>
                <li>R: Restart</li>
              </ul>
            </div>
          )}

          {activeTab === "leaderboard" && (
            <div className="panel-block leaderboard-panel">
              <div className="panel-title">🏆 Top Scores</div>
              {state.leaderboard.length === 0 ? (
                <div className="lb-empty">No scores yet</div>
              ) : (
                <div className="lb-list">
                  {state.leaderboard.slice(0, 5).map((entry, i) => (
                    <div
                      key={i}
                      className={`lb-row ${entry.name === state.playerName ? "lb-highlight" : ""}`}
                    >
                      <span className="lb-rank">
                        {i === 0 ? "🥇" : i === 1 ? "🥈" : i === 2 ? "🥉" : `${i + 1}.`}
                      </span>
                      <span className="lb-name">{entry.name}</span>
                      <span className="lb-score">{entry.score}</span>
                      <span className="lb-meta">L{entry.level}</span>
                    </div>
                  ))}
                </div>
              )}
            </div>
          )}
        </div>

      </div>
    </div>
  );
}

/* =============================================================================
 * NEXT PIECE PREVIEW
 * ============================================================================= */
function NextPiecePreview({ nextPiece }) {
  const shape     = nextPiece?.shape || [];
  const typeIndex = pieceTypeToIndex(nextPiece);
  const grid = Array.from({ length: PREVIEW_SIZE }, (_, r) =>
    Array.from({ length: PREVIEW_SIZE }, (_, c) => {
      const filled = shape[r]?.[c] ? 1 : 0;
      return filled ? typeIndex : 0;
    })
  );
  return (
    <div className="preview-grid">
      {grid.map((row, rowIndex) =>
        row.map((cell, colIndex) => (
          <div
            key={`${rowIndex}-${colIndex}`}
            className="cell"
            data-type={cell}
            data-ghost="false"
          />
        ))
      )}
    </div>
  );
}

/* =============================================================================
 * HOLD PIECE PREVIEW
 * ============================================================================= */
function HoldPiecePreview({ heldPiece, holdUsed }) {
  const shape     = heldPiece?.shape || [];
  const typeIndex = pieceTypeToIndex(heldPiece);
  const grid = Array.from({ length: PREVIEW_SIZE }, (_, r) =>
    Array.from({ length: PREVIEW_SIZE }, (_, c) => {
      const filled = shape[r]?.[c] ? 1 : 0;
      return filled ? typeIndex : 0;
    })
  );
  return (
    <div className={`preview-grid ${holdUsed ? "hold-dimmed" : ""}`}>
      {grid.map((row, rowIndex) =>
        row.map((cell, colIndex) => (
          <div
            key={`${rowIndex}-${colIndex}`}
            className="cell"
            data-type={cell}
            data-ghost="false"
          />
        ))
      )}
    </div>
  );
}