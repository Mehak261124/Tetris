import { useCallback, useEffect, useMemo, useReducer, useRef, useState } from "react";

const BOARD_ROWS = 20;
const BOARD_COLS = 10;
const PREVIEW_SIZE = 4;

const PIECE_INDEX = {
  I: 1,
  O: 2,
  T: 3,
  S: 4,
  Z: 5,
  J: 6,
  L: 7,
};

const WS_URL = import.meta.env.VITE_WS_URL || "ws://localhost:8080";

const emptyBoard = () =>
  Array.from({ length: BOARD_ROWS }, () => Array(BOARD_COLS).fill(0));

const initialState = {
  board: emptyBoard(),
  currentPiece: null,
  ghostPiece: null,
  nextPiece: null,
  score: 0,
  level: 1,
  lines: 0,
  gameState: "playing",
  wsStatus: "connecting",
};

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
        score: Number.isFinite(action.payload.score) ? action.payload.score : 0,
        level: Number.isFinite(action.payload.level) ? action.payload.level : 1,
        lines: Number.isFinite(action.payload.lines) ? action.payload.lines : 0,
        gameState: action.payload.state || "playing",
      };
    default:
      return state;
  }
};

const actionForKey = (event) => {
  const code = event.code;
  const key = event.key;

  if (code === "ArrowLeft" || key === "ArrowLeft" || key === "Left") return "move_left";
  if (code === "ArrowRight" || key === "ArrowRight" || key === "Right") return "move_right";
  if (code === "ArrowUp" || key === "ArrowUp" || key === "Up") return "rotate";
  if (code === "ArrowDown" || key === "ArrowDown" || key === "Down") return "soft_drop";
  if (code === "Space" || key === " " || key === "Spacebar") return "hard_drop";
  if (code === "KeyP" || key === "p" || key === "P") return "pause";
  if (code === "KeyR" || key === "r" || key === "R") return "restart";
  return null;
};

export default function App() {
  const [state, dispatch] = useReducer(reducer, initialState);
  const wsRef = useRef(null);
  const reconnectTimer = useRef(null);
  const [scoreFlash, setScoreFlash] = useState(false);
  const [lineFlash, setLineFlash] = useState(false);
  const [boardShake, setBoardShake] = useState(false);
  const prevLines = useRef(state.lines);

  const grid = useMemo(
    () => buildRenderGrid(state.board, state.currentPiece, state.ghostPiece),
    [state.board, state.currentPiece, state.ghostPiece]
  );

  useEffect(() => {
    if (!Number.isFinite(state.score)) return;
    setScoreFlash(true);
    const t = setTimeout(() => setScoreFlash(false), 200);
    return () => clearTimeout(t);
  }, [state.score]);

  useEffect(() => {
    if (!Number.isFinite(state.lines)) return;
    setLineFlash(true);
    const t = setTimeout(() => setLineFlash(false), 160);
    /* Screen shake on line clear */
    if (state.lines > prevLines.current) {
      setBoardShake(true);
      const t2 = setTimeout(() => setBoardShake(false), 250);
      prevLines.current = state.lines;
      return () => { clearTimeout(t); clearTimeout(t2); };
    }
    prevLines.current = state.lines;
    return () => clearTimeout(t);
  }, [state.lines]);

  useEffect(() => {
    let active = true;

    const connect = () => {
      if (!active) return;
      dispatch({ type: "WS_STATUS", status: "connecting" });
      const ws = new WebSocket(WS_URL);
      wsRef.current = ws;

      ws.onopen = () => dispatch({ type: "WS_STATUS", status: "open" });
      ws.onmessage = (event) => {
        try {
          const payload = JSON.parse(event.data);
          dispatch({ type: "UPDATE_STATE", payload });
        } catch (err) {
          dispatch({ type: "WS_STATUS", status: "error" });
        }
      };
      ws.onclose = () => {
        if (!active) return;
        dispatch({ type: "WS_STATUS", status: "reconnecting" });
        reconnectTimer.current = setTimeout(connect, 1500);
      };
      ws.onerror = () => {
        ws.close();
      };
    };

    connect();
    return () => {
      active = false;
      if (reconnectTimer.current) clearTimeout(reconnectTimer.current);
      if (wsRef.current) wsRef.current.close();
    };
  }, []);

  const sendAction = useCallback((action) => {
    const ws = wsRef.current;
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    ws.send(JSON.stringify({ action }));
  }, []);

  const handleKeyDown = useCallback(
    (event) => {
      const action = actionForKey(event);
      if (!action) return;
      if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Space"].includes(event.code)) {
        event.preventDefault();
      }
      sendAction(action);
    },
    [sendAction]
  );

  useEffect(() => {
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [handleKeyDown]);

  const showOverlay = state.gameState === "gameover";
  const showPaused = state.gameState === "paused";
  const wsMessage =
    state.wsStatus === "reconnecting"
      ? "Reconnecting..."
      : state.wsStatus === "connecting"
      ? "Connecting..."
      : state.wsStatus === "error"
      ? "Connection error"
      : "";

  const boardClasses = [
    "board-shell",
    "crt",
    lineFlash ? "lines-flash" : "",
    boardShake ? "board-shake" : "",
  ]
    .filter(Boolean)
    .join(" ");

  return (
    <div className="game-wrapper">
      <div className="game-layout">
        {/* -- Left: Game Board -- */}
        <div className="game-column">
          <div className="game-title">TETRIS</div>
          <div className={boardClasses}>
            <div className="scanlines" />
            <div className="board-grid">
              {grid.map((row, rowIndex) =>
                row.map((cell, colIndex) => {
                  const type = cell.active ? cell.activeType : cell.type;
                  const ghostType = cell.ghost ? cell.ghostType : 0;
                  const dataType = type || ghostType || 0;
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
                  <div className="overlay-title">PAUSED</div>
                  <button
                    className="overlay-button"
                    onClick={() => sendAction("pause")}
                  >
                    Resume
                  </button>
                </div>
              </div>
            )}
            {showOverlay && (
              <div className="overlay">
                <div className="overlay-card">
                  <div className="overlay-title">GAME OVER</div>
                  <button
                    className="overlay-button"
                    onClick={() => sendAction("restart")}
                  >
                    Restart
                  </button>
                </div>
              </div>
            )}
          </div>

          {/* -- Mobile Touch Controls -- */}
          <div className="touch-controls">
            <div className="touch-row">
              <button className="touch-btn" onClick={() => sendAction("move_left")}>◀</button>
              <button className="touch-btn" onClick={() => sendAction("rotate")}>▲</button>
              <button className="touch-btn" onClick={() => sendAction("soft_drop")}>▼</button>
              <button className="touch-btn" onClick={() => sendAction("move_right")}>▶</button>
            </div>
            <div className="touch-row">
              <button className="touch-btn touch-btn-wide" onClick={() => sendAction("hard_drop")}>DROP</button>
              <button className="touch-btn" onClick={() => sendAction("pause")}>⏸</button>
              <button className="touch-btn" onClick={() => sendAction("restart")}>↻</button>
            </div>
          </div>
        </div>

        {/* -- Right: Side Panel -- */}
        <div className="side-panel">
          <div className="panel-block">
            <div className="panel-title">Next Piece</div>
            <NextPiecePreview nextPiece={state.nextPiece} />
          </div>

          <div className="panel-block">
            <div className="panel-title">Score</div>
            <div className={`panel-value ${scoreFlash ? "score-flash" : ""}`}>
              {state.score}
            </div>
            <div className="panel-title" style={{ marginTop: "clamp(8px, 1.2vmin, 24px)" }}>Level</div>
            <div className="panel-value">{state.level}</div>
            <div className="panel-title" style={{ marginTop: "clamp(8px, 1.2vmin, 24px)" }}>Lines</div>
            <div className="panel-value">{state.lines}</div>
          </div>

          <div className="panel-block controls-desktop">
            <div className="panel-title">Controls</div>
            <ul className="controls">
              <li>← → : Move</li>
              <li>↑ : Rotate</li>
              <li>↓ : Soft Drop</li>
              <li>Space: Hard Drop</li>
              <li>P: Pause</li>
              <li>R: Restart</li>
            </ul>
          </div>
        </div>
      </div>
    </div>
  );
}

function NextPiecePreview({ nextPiece }) {
  const shape = nextPiece?.shape || [];
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
