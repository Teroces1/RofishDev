"""Runs tests: creates them, schedules games on a worker pool, records results.

* One test runs at a time (so every game gets the same share of the machine); any
  other started tests wait in a queue.
* Each worker plays one whole game (both engines pinned to the same CPU).
* All progress lives in SQLite, so a paused test can be resumed days later, with any
  openings you added in the meantime appended to its queue.
"""
import json
import random
import threading
import time
import traceback
from collections import deque
from dataclasses import dataclass, field

from . import settings as S
from . import stats, sysinfo
from .db import jloads
from .game import GameConfig, Player, play_game

ACTIVE_STATUSES = ("running", "queued", "pausing")


class ManagerError(Exception):
    def __init__(self, message, status=400, code=None, extra=None):
        super().__init__(message)
        self.message = message
        self.status = status
        self.code = code
        self.extra = extra or {}


@dataclass
class Job:
    idx: int
    opening: object
    cand_white: bool


@dataclass
class TestRun:
    test_id: int
    jobs: deque
    base_path: str
    cand_path: str
    inflight: dict = field(default_factory=dict)  # key -> (thread, cpu)
    stop_event: threading.Event = field(default_factory=threading.Event)
    pausing: bool = False
    pause_reason: str = ""
    end_status: str = None
    end_reason: str = None
    finish_times: deque = field(default_factory=lambda: deque(maxlen=400))
    started_at: float = field(default_factory=time.time)


class Manager:
    def __init__(self, db, store, book):
        self.db, self.store, self.book = db, store, book
        self.lock = threading.RLock()
        self.run = None
        self._stop = threading.Event()
        # anything that was running when the server last stopped is simply paused
        self.db.execute(
            "UPDATE tests SET status='paused', status_reason='server restarted' "
            "WHERE status IN ('running','queued','pausing')"
        )
        self.thread = threading.Thread(target=self._loop, daemon=True, name="sprt-scheduler")
        self.thread.start()

    # ------------------------------------------------------------------ helpers
    def _row(self, tid):
        row = self.db.one("SELECT * FROM tests WHERE id=?", (tid,))
        if row is None:
            raise ManagerError("test not found", 404)
        return row

    def _cfg(self, row):
        return S.with_test_defaults(jloads(row["settings"], {}))

    def log(self, tid, kind, message):
        self.db.execute(
            "INSERT INTO test_events(test_id, ts, kind, message) VALUES (?,?,?,?)",
            (tid, time.time(), kind, message),
        )

    def _set_status(self, tid, status, reason="", **extra):
        cols = {"status": status, "status_reason": reason, "updated_at": time.time(), **extra}
        sql = "UPDATE tests SET " + ", ".join(f"{k}=?" for k in cols) + " WHERE id=?"
        self.db.execute(sql, (*cols.values(), tid))

    # ------------------------------------------------------------------ creating / editing
    def create_test(self, base_id, cand_id, settings_in=None, experiment_id=None, label="", notes="",
                    allow_duplicate=False):
        base, cand = self.store.get(base_id), self.store.get(cand_id)
        if base is None:
            raise ManagerError(f"base engine {base_id!r} not found in engines/", 404)
        if cand is None:
            raise ManagerError(f"candidate engine {cand_id!r} not found in engines/", 404)
        try:
            cfg = {**S.test_defaults(self.db), **S.clean(S.TEST_SPEC, settings_in or {})}
        except ValueError as e:
            raise ManagerError(str(e))
        if not allow_duplicate:
            for r in self.db.query("SELECT id, settings FROM tests WHERE base_id=? AND cand_id=?", (base_id, cand_id)):
                if self._cfg(r)["movetime_ms"] == cfg["movetime_ms"]:
                    raise ManagerError(
                        f"Test #{r['id']} already compares these engines at {cfg['movetime_ms']} ms. "
                        "Resume it to add more games, or create a new one anyway.",
                        409, code="duplicate", extra={"existing_id": r["id"]},
                    )
        if experiment_id is not None and not self.db.one("SELECT id FROM experiments WHERE id=?", (experiment_id,)):
            raise ManagerError("experiment not found", 404)
        now = time.time()
        tid = self.db.execute(
            "INSERT INTO tests(experiment_id, label, notes, base_id, cand_id, base_sha, cand_sha, status, "
            "settings, seed, created_at, updated_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
            (experiment_id, label or "", notes or "", base_id, cand_id,
             self.store.sha_of(base_id), self.store.sha_of(cand_id), "paused", json.dumps(cfg),
             random.randrange(1, 2 ** 31), now, now),
        )
        self.sync_openings(tid)
        self._refresh_summary(tid)
        self.log(tid, "created", f"{cand_id} vs {base_id}, movetime {cfg['movetime_ms']} ms")
        return tid

    def update_test(self, tid, patch):
        t = self._row(tid)
        cfg = self._cfg(t)
        changes = []
        cols = {}
        if "settings" in patch:
            try:
                new = S.clean(S.TEST_SPEC, patch["settings"])
            except ValueError as e:
                raise ManagerError(str(e))
            if "movetime_ms" in new and new["movetime_ms"] != cfg["movetime_ms"] and t["games"] > 0:
                raise ManagerError("movetime can't change once games have been played - create a new test instead")
            for k, v in new.items():
                if cfg.get(k) != v:
                    changes.append(f"{k}: {cfg.get(k)} -> {v}")
            cfg.update(new)
            cols["settings"] = json.dumps(cfg)
        for k in ("label", "notes"):
            if k in patch:
                cols[k] = str(patch[k] or "")
        if "experiment_id" in patch:
            eid = patch["experiment_id"]
            if eid in ("", None):
                eid = None
            elif not self.db.one("SELECT id FROM experiments WHERE id=?", (eid,)):
                raise ManagerError("experiment not found", 404)
            cols["experiment_id"] = eid
        if cols:
            cols["updated_at"] = time.time()
            self.db.execute("UPDATE tests SET " + ", ".join(f"{k}=?" for k in cols) + " WHERE id=?", (*cols.values(), tid))
        if changes:
            self.log(tid, "settings", "; ".join(changes))
            self._refresh_summary(tid)
        return self._row(tid)

    def delete_test(self, tid):
        t = self._row(tid)
        if t["status"] in ACTIVE_STATUSES:
            raise ManagerError("pause the test before deleting it", 409)
        self.db.execute("DELETE FROM tests WHERE id=?", (tid,))

    # ------------------------------------------------------------------ openings
    def sync_openings(self, tid):
        """Append openings found in openings/ that this test doesn't know yet (shuffled)."""
        t = self._row(tid)
        with self.lock:
            self.book.scan()
            existing = {r["hash"] for r in self.db.query("SELECT hash FROM test_openings WHERE test_id=?", (tid,))}
            max_idx = self.db.one("SELECT COALESCE(MAX(idx), -1) AS m FROM test_openings WHERE test_id=?", (tid,))["m"]
            new = [h for h in self.book.hashes() if h not in existing]
            random.Random(t["seed"] * 1000003 + len(existing)).shuffle(new)
            with self.db.transaction():
                for i, h in enumerate(new):
                    self.db.execute(
                        "INSERT INTO test_openings(test_id, idx, hash, name) VALUES (?,?,?,?)",
                        (tid, max_idx + 1 + i, h, self.book.get(h).name),
                    )
            total = len(existing) + len(new)
            self.db.execute("UPDATE tests SET openings_total=? WHERE id=?", (total, tid))
        return len(new), total

    def _build_jobs(self, tid):
        rows = self.db.query("SELECT idx, hash FROM test_openings WHERE test_id=? ORDER BY idx", (tid,))
        done = {(r["opening_hash"], r["cand_white"]) for r in
                self.db.query("SELECT opening_hash, cand_white FROM games WHERE test_id=?", (tid,))}
        jobs = deque()
        for r in rows:
            op = self.book.get(r["hash"])
            if op is None:
                continue  # file was removed from openings/
            for cw in (1, 0):
                if (r["hash"], cw) not in done:
                    jobs.append(Job(r["idx"], op, bool(cw)))
        return jobs

    def opening_progress(self, tid):
        total = self.db.one("SELECT COUNT(*) AS n FROM test_openings WHERE test_id=?", (tid,))["n"]
        played = self.db.one("SELECT COUNT(DISTINCT opening_hash) AS n FROM games WHERE test_id=?", (tid,))["n"]
        complete = self.db.one(
            "SELECT COUNT(*) AS n FROM (SELECT opening_hash FROM games WHERE test_id=? "
            "GROUP BY opening_hash HAVING COUNT(*)=2)", (tid,))["n"]
        return {"total": total, "started": played, "complete_pairs": complete, "unplayed": total - played}

    # ------------------------------------------------------------------ controls
    def start_test(self, tid, force=False):
        with self.lock:
            t = self._row(tid)
            if t["status"] in ACTIVE_STATUSES:
                return t
            base, cand = self.store.get(t["base_id"]), self.store.get(t["cand_id"])
            if base is None or cand is None:
                raise ManagerError("an engine folder for this test is missing from engines/", 409)
            cur_b, cur_c = self.store.sha_of(t["base_id"]), self.store.sha_of(t["cand_id"])
            if (t["base_sha"] != cur_b or t["cand_sha"] != cur_c) and not force:
                which = [n for n, a, b in (("base", t["base_sha"], cur_b), ("candidate", t["cand_sha"], cur_c)) if a != b]
                raise ManagerError(
                    f"The {' and '.join(which)} executable changed since this test started, so new games "
                    "would not be comparable with the old ones.",
                    409, code="binary_changed",
                )
            added, total = self.sync_openings(tid)
            self._refresh_summary(tid)
            t = self._row(tid)
            cfg = self._cfg(t)
            allowed = (cfg["max_games"] // 2) * 2
            if cfg["auto_stop"] and t["verdict"] != "inconclusive":
                raise ManagerError(
                    f"The result is already decided ({t['verdict']}). Turn off auto-stop or change the "
                    "equal band / error rates to keep testing.", 409, code="decided")
            if t["games"] >= allowed:
                raise ManagerError(f"Max games ({allowed}) already reached. Raise max games to continue.", 409, code="max_games")
            if not self._build_jobs(tid):
                raise ManagerError("Every opening has already been played. Add opening files to openings/ and resume.", 409, code="no_openings")
            if force and (t["base_sha"] != cur_b or t["cand_sha"] != cur_c):
                # only accept the new binaries once we know the test can really start
                self.db.execute("UPDATE tests SET base_sha=?, cand_sha=? WHERE id=?", (cur_b, cur_c, tid))
                self.log(tid, "binary_changed", "engine executable changed; continuing with the new binary (forced)")
            first = t["started_at"] is None
            self._set_status(tid, "queued", "", queued_at=time.time(), started_at=t["started_at"] or time.time())
            self.log(tid, "queued", ("started" if first else "resumed") + (f" (+{added} new openings)" if added else ""))
            return self._row(tid)

    def pause_test(self, tid, now=False):
        with self.lock:
            t = self._row(tid)
            run = self.run
            if run is not None and run.test_id == tid:
                run.pausing = True
                run.pause_reason = "paused by user"
                if now:
                    run.stop_event.set()
                self._set_status(tid, "pausing", "waiting for running games to finish" if not now else "stopping games")
                self.log(tid, "pause", "pause requested" + (" (discarding unfinished games)" if now else ""))
            elif t["status"] == "queued":
                self._set_status(tid, "paused", "removed from queue")
            return self._row(tid)

    def live(self, tid):
        with self.lock:
            run = self.run
            if run is None or run.test_id != tid:
                return None
            now = time.time()
            recent = [x for x in run.finish_times if now - x <= 60]
            rate = None
            if len(recent) >= 2 and now - recent[0] > 5:
                rate = len(recent) / max(now - recent[0], 1e-6) * 60.0
            t = self._row(tid)
            cfg = self._cfg(t)
            allowed = (cfg["max_games"] // 2) * 2
            remaining = max(0, min(allowed - t["games"], len(run.jobs) + len(run.inflight)))
            return {
                "inflight": len(run.inflight),
                "queued_jobs": len(run.jobs),
                "games_per_min": rate,
                "eta_seconds": (remaining / rate * 60.0) if rate else None,
                "workers": S.load_global(self.db)["workers"],
            }

    # ------------------------------------------------------------------ scheduler
    def _loop(self):
        while not self._stop.is_set():
            try:
                self._tick()
            except Exception:
                traceback.print_exc()
            time.sleep(0.2)

    def _begin(self, tid):
        t = self._row(tid)
        base, cand = self.store.get(t["base_id"]), self.store.get(t["cand_id"])
        if base is None or cand is None:
            self._set_status(tid, "aborted", "engine executable missing", finished_at=time.time())
            self.log(tid, "aborted", "engine executable missing")
            return
        self.run = TestRun(tid, self._build_jobs(tid), base["exe_path"], cand["exe_path"])
        self._set_status(tid, "running", "")
        self.log(tid, "running", f"{len(self.run.jobs)} games waiting")

    def _tick(self):
        with self.lock:
            if self.run is None:
                nxt = self.db.one("SELECT id FROM tests WHERE status='queued' ORDER BY queued_at LIMIT 1")
                if nxt:
                    self._begin(nxt["id"])
            run = self.run
            if run is None:
                return
            t = self._row(run.test_id)
            cfg = self._cfg(t)
            gset = S.load_global(self.db)
            allowed = (cfg["max_games"] // 2) * 2
            if not run.pausing and run.end_status is None:
                pool = sysinfo.cpu_pool(gset)
                while run.jobs and len(run.inflight) < gset["workers"] and t["games"] + len(run.inflight) < allowed:
                    used = {c for (_th, c) in run.inflight.values() if c is not None}
                    free = [c for c in pool if c not in used]
                    cpu = free[0] if free else None
                    job = run.jobs.popleft()
                    key = (job.opening.hash, job.cand_white)
                    th = threading.Thread(target=self._game_worker, args=(run, job, cpu, cfg), daemon=True)
                    run.inflight[key] = (th, cpu)
                    th.start()
            if run.inflight:
                return
            if run.pausing:
                self._end(run, "paused", run.pause_reason or "paused")
            elif run.end_status:
                self._end(run, run.end_status, run.end_reason)
            elif t["games"] >= allowed:
                self._end(run, "finished", f"max games reached ({allowed}) without a decision")
            elif not run.jobs:
                self._end(run, "finished", "all openings played without a decision - add openings and resume")

    def _end(self, run, status, reason):
        tid = run.test_id
        extra = {"finished_at": time.time()} if status in ("finished", "aborted") else {}
        self._set_status(tid, status, reason, **extra)
        self.log(tid, status, reason)
        self.run = None
        if status in ("finished", "aborted"):
            self._notify(tid, status, reason)

    def _game_worker(self, run, job, cpu, cfg):
        key = (job.opening.hash, job.cand_white)
        outcome = None
        failure = None
        try:
            cand = Player(run.cand_path, self._row(run.test_id)["cand_id"], "candidate")
            base = Player(run.base_path, self._row(run.test_id)["base_id"], "base")
            white, black = (cand, base) if job.cand_white else (base, cand)
            outcome = play_game(white, black, job.opening, GameConfig.from_settings(cfg), cpu, run.stop_event)
        except Exception:
            failure = traceback.format_exc()
        with self.lock:
            run.inflight.pop(key, None)
            if failure:
                traceback.print_exc()
                if run.end_status is None:
                    run.end_status, run.end_reason = "aborted", "internal error while playing a game (see server console)"
                    run.stop_event.set()
                self.log(run.test_id, "internal_error", failure[-1500:])
                return
            if outcome is None:
                return  # discarded (pause now / test ended)
            self._record_game(run, job, outcome)

    # ------------------------------------------------------------------ results
    def _record_game(self, run, job, o):
        tid = run.test_id
        cw = job.cand_white
        if o.result == "1/2-1/2":
            pts = 0.5
        elif (o.result == "1-0") == cw:
            pts = 1.0
        else:
            pts = 0.0
        try:
            self.db.execute(
                "INSERT INTO games(test_id, opening_hash, opening_idx, opening_name, cand_white, result, "
                "cand_points, termination, plies, opening_plies, start_fen, moves, evals, error, duration_ms, "
                "finished_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (tid, job.opening.hash, job.idx, job.opening.name, int(cw), o.result, pts, o.termination,
                 len(o.moves), o.opening_plies, o.start_fen, json.dumps(o.moves), json.dumps(o.evals),
                 json.dumps(o.error) if o.error else None, o.duration_ms, time.time()),
            )
        except Exception:
            traceback.print_exc()  # UNIQUE violation = duplicate game, ignore
            return
        run.finish_times.append(time.time())
        if o.error:
            e = o.error
            self.log(tid, "engine_error",
                     f"{e['kind']} by {e['role']} ({e['engine_id']}) as {e['color']} at ply {e['ply']}: {e['message']}")
        t = self._refresh_summary(tid)
        cfg = self._cfg(t)
        errors = t["errors_base"] + t["errors_cand"]
        if run.end_status is None:
            if cfg["error_abort_limit"] > 0 and errors >= cfg["error_abort_limit"]:
                run.end_status = "aborted"
                run.end_reason = (f"{errors} engine errors (illegal moves, crashes, timeouts); "
                                  f"limit is {cfg['error_abort_limit']}")
                run.stop_event.set()
            elif cfg["auto_stop"] and t["verdict"] != "inconclusive":
                run.end_status = "finished"
                run.end_reason = f"SPRT decided: {t['verdict']}"
                run.stop_event.set()

    def _refresh_summary(self, tid):
        t = self._row(tid)
        cfg = self._cfg(t)
        rows = self.db.query("SELECT opening_hash, cand_points FROM games WHERE test_id=?", (tid,))
        w = d = l = 0
        pairs = {}
        for r in rows:
            p = r["cand_points"]
            if p == 1.0:
                w += 1
            elif p == 0.5:
                d += 1
            else:
                l += 1
            pairs.setdefault(r["opening_hash"], []).append(p)
        penta = [0] * 5
        for pts in pairs.values():
            if len(pts) == 2:
                penta[stats.pair_bucket(sum(pts))] += 1
        eb = ec = 0
        for r in self.db.query("SELECT error FROM games WHERE test_id=? AND error IS NOT NULL", (tid,)):
            err = jloads(r["error"], {})
            if err.get("role") == "base":
                eb += 1
            else:
                ec += 1
        ev = stats.evaluate(penta, cfg["band_elo"], cfg["alpha"], cfg["beta"], cfg["min_games"])
        self.db.execute(
            "UPDATE tests SET games=?, wins=?, draws=?, losses=?, penta=?, llr_up=?, llr_down=?, verdict=?, "
            "elo=?, elo_margin=?, los=?, errors_base=?, errors_cand=?, updated_at=? WHERE id=?",
            (w + d + l, w, d, l, json.dumps(penta), ev["llr_up"], ev["llr_down"], ev["verdict"],
             ev["elo"], ev["margin"], ev["los"], eb, ec, time.time(), tid),
        )
        return self._row(tid)

    # ------------------------------------------------------------------ notifications
    def _notify(self, tid, status, reason):
        g = S.load_global(self.db)
        if not g["discord_webhook_url"]:
            return
        if status == "finished" and not g["notify_finish"]:
            return
        if status == "aborted" and not g["notify_abort"]:
            return
        t = self._row(tid)
        elo = "n/a" if t["elo"] is None else f"{t['elo']:+.1f}" + (f" +/- {t['elo_margin']:.1f}" if t["elo_margin"] else "")
        verdict = {"better": "Definitely better", "worse": "Definitely worse", "equal": "Equal",
                   "inconclusive": "Inconclusive"}[t["verdict"]]
        colors = {"better": 0x2EA043, "worse": 0xDA3633, "equal": 0x768390, "inconclusive": 0xD29922}
        color = 0xDA3633 if status == "aborted" else colors[t["verdict"]]
        url = f"{g['public_url'].rstrip('/')}/#/test/{tid}"
        title = f"Test #{tid} {'ABORTED' if status == 'aborted' else 'finished'}: {verdict}" if status == "finished" \
            else f"Test #{tid} ABORTED"
        sysinfo.send_discord(
            g["discord_webhook_url"], title,
            f"**{t['cand_id']}** vs **{t['base_id']}**\n{reason}\n{url}",
            color=color,
            fields=[("Elo", elo), ("Games", t["games"]), ("W / D / L", f"{t['wins']} / {t['draws']} / {t['losses']}"),
                    ("Engine errors", t["errors_base"] + t["errors_cand"])],
        )

    def shutdown(self):
        self._stop.set()
        with self.lock:
            if self.run:
                self.run.stop_event.set()
