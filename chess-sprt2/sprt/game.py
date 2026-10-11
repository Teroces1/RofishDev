"""Plays one game between two engine executables.

python-chess is the referee: it generates the legal moves, so neither engine is trusted.
An engine that returns an illegal/unparseable move, crashes, or hangs past the hard
timeout loses immediately, and a detailed error report is attached to the result.
"""
import re
import time
from dataclasses import dataclass, field

import chess

from .engine_proc import EngineProcess

SCORE_RE = re.compile(r"\bscore\s+(cp|mate)\s+(-?\d+)")
DEPTH_RE = re.compile(r"\bdepth\s+(\d+)")
MATE_CP = 20000


@dataclass
class Player:
    path: str
    engine_id: str
    role: str  # "base" or "candidate"


@dataclass
class GameConfig:
    movetime_ms: int = 100
    hard_timeout_factor: float = 10.0
    hard_timeout_min_ms: int = 2000
    startup_grace_ms: int = 2000
    max_plies: int = 600
    score_perspective: str = "white"  # "white": scores are always from white's view; "stm": side to move
    adj_win_enabled: bool = False
    adj_win_cp: int = 1000
    adj_win_moves: int = 8
    adj_draw_enabled: bool = False
    adj_draw_cp: int = 10
    adj_draw_moves: int = 10
    adj_draw_min_move: int = 30

    @classmethod
    def from_settings(cls, s):
        return cls(**{k: s[k] for k in cls.__dataclass_fields__ if k in s})

    @property
    def hard_timeout_ms(self):
        return max(self.hard_timeout_min_ms, self.hard_timeout_factor * self.movetime_ms)


@dataclass
class GameOutcome:
    result: str            # '1-0', '0-1', '1/2-1/2'
    termination: str
    moves: list            # every uci move, opening included
    evals: list           # per ply: None (opening) or [white_pov_cp|None, depth|None, ms]
    opening_plies: int
    start_fen: str
    duration_ms: int
    error: dict = None     # filled for illegal_move / crash / timeout


def parse_info_score(infos, white_to_move, perspective):
    """Score + depth from the LAST completed search line.

    Lines containing '[null]' are the engine's summary of an unfinished search; they are
    skipped so the evaluation comes from the last fully searched depth.
    Returns (score_white_pov or None, depth or None).
    """
    for line in reversed(infos):
        if "[null]" in line:
            continue
        m = SCORE_RE.search(line)
        if not m:
            continue
        val = int(m.group(2))
        if m.group(1) == "mate":
            val = (MATE_CP - abs(val)) * (1 if val > 0 else -1)
        if perspective == "stm" and not white_to_move:
            val = -val
        d = DEPTH_RE.search(line)
        return val, (int(d.group(1)) if d else None)
    return None, None


def _terminal(board, n_plies, cfg):
    """Rules-based game end (no engine involvement). Returns (result, reason) or None."""
    if board.is_checkmate():
        return ("0-1" if board.turn == chess.WHITE else "1-0"), "checkmate"
    if board.is_stalemate():
        return "1/2-1/2", "stalemate"
    if board.is_insufficient_material():
        return "1/2-1/2", "insufficient_material"
    if board.is_repetition(3):
        return "1/2-1/2", "threefold_repetition"
    if board.is_fifty_moves():
        return "1/2-1/2", "fifty_move_rule"
    if n_plies >= cfg.max_plies:
        return "1/2-1/2", "max_plies"
    return None


def _adjudicate(evals, cfg):
    if not (cfg.adj_win_enabled or cfg.adj_draw_enabled):
        return None
    scores = [e[0] if e else None for e in evals]
    if cfg.adj_win_enabled:
        n = 2 * cfg.adj_win_moves
        last = scores[-n:]
        if len(last) == n and all(s is not None for s in last):
            if all(s >= cfg.adj_win_cp for s in last):
                return "1-0", "adjudication_win"
            if all(s <= -cfg.adj_win_cp for s in last):
                return "0-1", "adjudication_win"
    if cfg.adj_draw_enabled and len(evals) >= 2 * cfg.adj_draw_min_move:
        n = 2 * cfg.adj_draw_moves
        last = scores[-n:]
        if len(last) == n and all(s is not None for s in last) and all(abs(s) <= cfg.adj_draw_cp for s in last):
            return "1/2-1/2", "adjudication_draw"
    return None


def play_game(white: Player, black: Player, opening, cfg: GameConfig, cpu=None, stop_event=None):
    """Play one game. Returns GameOutcome, or None if `stop_event` aborted it."""
    t0 = time.monotonic()
    board = chess.Board(opening.fen) if opening.fen else chess.Board()
    start_fen = board.fen()
    moves, evals = [], []
    for u in opening.moves:
        board.push_uci(u)
        moves.append(u)
        evals.append(None)
    opening_plies = len(moves)

    players = {chess.WHITE: white, chess.BLACK: black}
    procs = {}
    try:
        for color, pl in players.items():
            procs[color] = EngineProcess(pl.path, f"{pl.role}:{pl.engine_id}", cpu=cpu)
    except OSError as e:
        for p in procs.values():
            p.kill()
        # failing to launch is a crash of that engine
        bad = chess.WHITE if color == chess.WHITE else chess.BLACK
        err = _report("crash", players[bad], bad, board, moves, None, [], f"could not start engine: {e}", None, None)
        return _finish(("0-1" if bad == chess.WHITE else "1-0"), "crash", moves, evals, opening_plies, start_fen, t0, err)

    first_move = {chess.WHITE: True, chess.BLACK: True}
    try:
        while True:
            end = _terminal(board, len(moves), cfg) or _adjudicate(evals, cfg)
            if end:
                return _finish(end[0], end[1], moves, evals, opening_plies, start_fen, t0)

            color = board.turn
            pl, proc = players[color], procs[color]
            cmd = f"position fen {start_fen}" + (" moves " + " ".join(moves) if moves else "")
            go = f"go movetime {cfg.movetime_ms}"
            proc.drain()
            ok = proc.send(cmd) and proc.send(go)

            timeout_ms = cfg.hard_timeout_ms + (cfg.startup_grace_ms if first_move[color] else 0)
            first_move[color] = False
            if ok:
                kind, line, infos, elapsed = proc.wait_bestmove(timeout_ms / 1000.0, stop_event)
            else:
                kind, line, infos, elapsed = "eof", None, [], 0.0

            if kind == "stopped":
                return None

            loser_result = "0-1" if color == chess.WHITE else "1-0"
            if kind in ("eof", "timeout"):
                if kind == "timeout":
                    proc.kill()
                else:
                    proc.kill()  # reap + collect exit code
                reason = "crash" if kind == "eof" else "timeout"
                msg = ("engine process ended before printing bestmove" if kind == "eof"
                       else f"no bestmove within {timeout_ms:.0f} ms (movetime {cfg.movetime_ms} ms)")
                err = _report(reason, pl, color, board, moves, None, infos, msg, proc, elapsed, cmd)
                return _finish(loser_result, reason, moves, evals, opening_plies, start_fen, t0, err)

            parts = line.split()
            token = parts[1] if len(parts) > 1 else ""
            try:
                mv = chess.Move.from_uci(token)
            except ValueError:
                mv = None
            if mv is None or mv not in board.legal_moves:
                msg = f"illegal or unparseable move {token!r}"
                err = _report("illegal_move", pl, color, board, moves, line, infos, msg, proc, elapsed, cmd)
                proc.kill()
                return _finish(loser_result, "illegal_move", moves, evals, opening_plies, start_fen, t0, err)

            score, depth = parse_info_score(infos, color == chess.WHITE, cfg.score_perspective)
            moves.append(mv.uci())
            evals.append([score, depth, round(elapsed)])
            board.push(mv)
    finally:
        for p in procs.values():
            p.close()


def _finish(result, term, moves, evals, opening_plies, start_fen, t0, error=None):
    return GameOutcome(
        result=result, termination=term, moves=moves, evals=evals,
        opening_plies=opening_plies, start_fen=start_fen,
        duration_ms=int((time.monotonic() - t0) * 1000), error=error,
    )


def _report(kind, player, color, board, moves, bestmove_line, infos, message, proc, elapsed, cmd=None):
    """Detailed error report shown in the UI's error log."""
    exit_code = None
    tail = []
    if proc is not None:
        exit_code = proc.proc.poll()
        tail = list(proc.tail)
    return {
        "kind": kind,
        "message": message,
        "engine_id": player.engine_id,
        "role": player.role,
        "color": "white" if color == chess.WHITE else "black",
        "ply": len(moves) + 1,
        "fen_before": board.fen(),
        "moves_so_far": " ".join(moves),
        "command_sent": cmd,
        "bestmove_line": bestmove_line,
        "legal_moves": sorted(m.uci() for m in board.legal_moves),
        "elapsed_ms": None if elapsed is None else round(elapsed),
        "exit_code": exit_code,
        "last_info_lines": infos[-5:],
        "output_tail": tail,
    }
