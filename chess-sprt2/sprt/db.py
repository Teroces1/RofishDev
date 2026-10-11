import json
import sqlite3
import threading
from contextlib import contextmanager

SCHEMA = """
CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);

CREATE TABLE IF NOT EXISTS experiments (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  description TEXT DEFAULT '',
  created_at REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS tests (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  experiment_id INTEGER REFERENCES experiments(id) ON DELETE SET NULL,
  label TEXT DEFAULT '',
  notes TEXT DEFAULT '',
  base_id TEXT NOT NULL,
  cand_id TEXT NOT NULL,
  base_sha TEXT,
  cand_sha TEXT,
  status TEXT NOT NULL DEFAULT 'paused',
  status_reason TEXT DEFAULT '',
  settings TEXT NOT NULL,
  seed INTEGER NOT NULL,
  created_at REAL, queued_at REAL, started_at REAL, updated_at REAL, finished_at REAL,
  games INTEGER DEFAULT 0,
  wins INTEGER DEFAULT 0,
  draws INTEGER DEFAULT 0,
  losses INTEGER DEFAULT 0,
  penta TEXT DEFAULT '[0,0,0,0,0]',
  llr_up REAL DEFAULT 0,
  llr_down REAL DEFAULT 0,
  verdict TEXT DEFAULT 'inconclusive',
  elo REAL, elo_margin REAL, los REAL,
  errors_base INTEGER DEFAULT 0,
  errors_cand INTEGER DEFAULT 0,
  openings_total INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_tests_base ON tests(base_id);
CREATE INDEX IF NOT EXISTS idx_tests_cand ON tests(cand_id);

CREATE TABLE IF NOT EXISTS test_openings (
  test_id INTEGER NOT NULL REFERENCES tests(id) ON DELETE CASCADE,
  idx INTEGER NOT NULL,
  hash TEXT NOT NULL,
  name TEXT,
  PRIMARY KEY (test_id, idx),
  UNIQUE (test_id, hash)
);

CREATE TABLE IF NOT EXISTS games (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  test_id INTEGER NOT NULL REFERENCES tests(id) ON DELETE CASCADE,
  opening_hash TEXT NOT NULL,
  opening_idx INTEGER,
  opening_name TEXT,
  cand_white INTEGER NOT NULL,
  result TEXT NOT NULL,
  cand_points REAL NOT NULL,
  termination TEXT NOT NULL,
  plies INTEGER,
  opening_plies INTEGER,
  start_fen TEXT,
  moves TEXT NOT NULL,
  evals TEXT,
  error TEXT,
  duration_ms INTEGER,
  finished_at REAL,
  UNIQUE (test_id, opening_hash, cand_white)
);
CREATE INDEX IF NOT EXISTS idx_games_test ON games(test_id, id);

CREATE TABLE IF NOT EXISTS test_events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  test_id INTEGER NOT NULL REFERENCES tests(id) ON DELETE CASCADE,
  ts REAL NOT NULL,
  kind TEXT NOT NULL,
  message TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_events_test ON test_events(test_id, id);
"""


class DB:
    """A single SQLite connection guarded by a lock (plenty fast for this workload)."""

    def __init__(self, path):
        self.conn = sqlite3.connect(str(path), check_same_thread=False, isolation_level=None)
        self.conn.row_factory = sqlite3.Row
        self.lock = threading.RLock()
        with self.lock:
            self.conn.execute("PRAGMA journal_mode=WAL")
            self.conn.execute("PRAGMA synchronous=NORMAL")
            self.conn.execute("PRAGMA foreign_keys=ON")
            self.conn.executescript(SCHEMA)

    def execute(self, sql, params=()):
        with self.lock:
            cur = self.conn.execute(sql, params)
            return cur.lastrowid

    def query(self, sql, params=()):
        with self.lock:
            return [dict(r) for r in self.conn.execute(sql, params).fetchall()]

    def one(self, sql, params=()):
        rows = self.query(sql, params)
        return rows[0] if rows else None

    @contextmanager
    def transaction(self):
        with self.lock:
            self.conn.execute("BEGIN")
            try:
                yield
            except BaseException:
                self.conn.execute("ROLLBACK")
                raise
            else:
                self.conn.execute("COMMIT")


def jloads(s, default=None):
    if s is None or s == "":
        return default
    try:
        return json.loads(s)
    except ValueError:
        return default
