"""Loads every *.json file in openings/ and validates each opening with python-chess.

File format (same as your openings.json):
    [ {"opening": "Caro-Kann Defense: Goldman Variation", "moves": ["e2e4", "c7c6", ...]}, ... ]
An entry may also carry an optional "fen" (start position); the default is the standard one.

Openings are identified by a hash of (start fen + move list), so the same line in two
files is only used once, and renaming an opening never makes it replay.
"""
import hashlib
import json
import threading
from dataclasses import dataclass
from pathlib import Path

import chess


@dataclass
class Opening:
    hash: str
    name: str
    moves: list
    fen: str  # None = standard start position
    file: str


def opening_hash(fen, moves):
    raw = (fen or "startpos") + "|" + " ".join(moves)
    return hashlib.sha1(raw.encode()).hexdigest()[:16]


def validate(fen, moves):
    """Return an error string, or None when the line is legal and the game isn't over."""
    try:
        board = chess.Board(fen) if fen else chess.Board()
    except ValueError as e:
        return f"bad fen: {e}"
    for i, u in enumerate(moves):
        try:
            mv = chess.Move.from_uci(u)
        except ValueError:
            return f"move {i + 1} ({u!r}) is not valid UCI"
        if mv not in board.legal_moves:
            return f"move {i + 1} ({u}) is illegal"
        board.push(mv)
    if board.is_game_over(claim_draw=False):
        return "game is already over after the opening"
    return None


class OpeningBook:
    def __init__(self, directory):
        self.dir = Path(directory)
        self.lock = threading.RLock()
        self.openings = {}  # hash -> Opening, in file order
        self.invalid = []   # [{file, index, name, reason}]
        self.files = []     # [{file, total, valid, duplicates}]
        self.scan()

    def scan(self):
        openings, invalid, files = {}, [], []
        self.dir.mkdir(exist_ok=True)
        for path in sorted(self.dir.glob("*.json")):
            total = valid = dups = 0
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                if not isinstance(data, list):
                    raise ValueError("top level must be a list")
            except (ValueError, OSError) as e:
                invalid.append({"file": path.name, "index": None, "name": None, "reason": f"unreadable file: {e}"})
                files.append({"file": path.name, "total": 0, "valid": 0, "duplicates": 0, "error": str(e)})
                continue
            for i, entry in enumerate(data):
                total += 1
                try:
                    moves = [str(m) for m in entry["moves"]]
                    name = str(entry.get("opening", f"opening {i + 1}"))
                    fen = entry.get("fen") or None
                except (KeyError, TypeError, AttributeError):
                    invalid.append({"file": path.name, "index": i, "name": None, "reason": "entry needs a 'moves' list"})
                    continue
                reason = validate(fen, moves)
                if reason:
                    invalid.append({"file": path.name, "index": i, "name": name, "reason": reason})
                    continue
                h = opening_hash(fen, moves)
                if h in openings:
                    dups += 1
                    continue
                openings[h] = Opening(h, name, moves, fen, path.name)
                valid += 1
            files.append({"file": path.name, "total": total, "valid": valid, "duplicates": dups})
        with self.lock:
            self.openings, self.invalid, self.files = openings, invalid, files

    def get(self, h):
        with self.lock:
            return self.openings.get(h)

    def hashes(self):
        with self.lock:
            return list(self.openings.keys())

    def summary(self):
        with self.lock:
            return {
                "dir": str(self.dir),
                "total": len(self.openings),
                "files": list(self.files),
                "invalid": self.invalid[:200],
                "invalid_count": len(self.invalid),
            }
