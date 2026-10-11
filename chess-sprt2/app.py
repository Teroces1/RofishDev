"""SPRT tester for UCI chess engines - Flask app.

    python app.py                 # http://127.0.0.1:5000
    python app.py --port 8080
"""
import argparse
import io
import time
from pathlib import Path

import chess
import chess.pgn
from flask import Flask, Response, jsonify, request, send_from_directory

from sprt import settings as S
from sprt import stats, sysinfo
from sprt.db import DB, jloads
from sprt.engines import EngineStore
from sprt.manager import Manager, ManagerError
from sprt.openings import OpeningBook

ROOT = Path(__file__).resolve().parent
(ROOT / "data").mkdir(exist_ok=True)

db = DB(ROOT / "data" / "sprt.db")
store = EngineStore(ROOT / "engines")
book = OpeningBook(ROOT / "openings")
manager = Manager(db, store, book)

app = Flask(__name__, static_folder=str(ROOT / "static"), static_url_path="/static")
app.config["MAX_CONTENT_LENGTH"] = 512 * 1024 * 1024  # engine uploads


# ----------------------------------------------------------------------------- helpers
@app.errorhandler(ManagerError)
def _manager_error(e):
    return jsonify({"error": e.message, "code": e.code, **e.extra}), e.status


@app.errorhandler(ValueError)
def _value_error(e):
    return jsonify({"error": str(e)}), 400


def body():
    return request.get_json(silent=True) or {}


def engine_names():
    return {e["id"]: e["name"] for e in store.scan()}


def test_dict(t, names=None, experiments=None):
    cfg = S.with_test_defaults(jloads(t["settings"], {}))
    lower, upper = stats.sprt_bounds(cfg["alpha"], cfg["beta"])
    penta = jloads(t["penta"], [0] * 5)
    games = t["games"]
    names = names if names is not None else engine_names()
    exp_name = None
    if t["experiment_id"] is not None:
        exp_name = (experiments or {}).get(t["experiment_id"])
        if exp_name is None:
            row = db.one("SELECT name FROM experiments WHERE id=?", (t["experiment_id"],))
            exp_name = row["name"] if row else None
    return {
        "id": t["id"], "label": t["label"], "notes": t["notes"],
        "experiment_id": t["experiment_id"], "experiment_name": exp_name,
        "base_id": t["base_id"], "cand_id": t["cand_id"],
        "base_name": names.get(t["base_id"], t["base_id"]), "cand_name": names.get(t["cand_id"], t["cand_id"]),
        "self_test": t["base_id"] == t["cand_id"],
        "status": t["status"], "status_reason": t["status_reason"], "settings": cfg,
        "games": games, "wins": t["wins"], "draws": t["draws"], "losses": t["losses"],
        "penta": penta, "pairs": sum(penta),
        "draw_ratio": (t["draws"] / games) if games else None,
        "llr_up": t["llr_up"], "llr_down": t["llr_down"], "lower": lower, "upper": upper,
        "verdict": t["verdict"], "elo": t["elo"], "elo_margin": t["elo_margin"], "los": t["los"],
        "errors_base": t["errors_base"], "errors_cand": t["errors_cand"],
        "openings_total": t["openings_total"],
        "created_at": t["created_at"], "started_at": t["started_at"],
        "updated_at": t["updated_at"], "finished_at": t["finished_at"],
    }


def game_row_light(g):
    p = g["cand_points"]
    err = None
    if g.get("error"):
        err = jloads(g["error"], {})
    return {
        "id": g["id"], "opening_name": g["opening_name"], "cand_white": bool(g["cand_white"]),
        "outcome": "W" if p == 1.0 else ("D" if p == 0.5 else "L"),
        "result": g["result"], "termination": g["termination"], "plies": g["plies"],
        "duration_ms": g["duration_ms"], "finished_at": g["finished_at"],
        "error_kind": err["kind"] if err else None,
        "error": err,
    }


def build_pgn(g, t, names):
    board = chess.Board(g["start_fen"]) if g["start_fen"] else chess.Board()
    game = chess.pgn.Game()
    game.setup(board)
    cand = names.get(t["cand_id"], t["cand_id"])
    base = names.get(t["base_id"], t["base_id"])
    white, black = (cand, base) if g["cand_white"] else (base, cand)
    game.headers.update({
        "Event": f"SPRT test #{t['id']}", "Site": "local", "Round": str(g["id"]),
        "White": white, "Black": black, "Result": g["result"],
        "Opening": g["opening_name"] or "", "Termination": g["termination"],
        "TimeControl": f"{S.with_test_defaults(jloads(t['settings'], {}))['movetime_ms']}ms/move",
    })
    moves = jloads(g["moves"], [])
    evals = jloads(g["evals"], [])
    node = game
    b = board.copy()
    for i, u in enumerate(moves):
        mv = chess.Move.from_uci(u)
        node = node.add_variation(mv)
        b.push(mv)
        e = evals[i] if i < len(evals) else None
        if e and e[0] is not None:
            node.comment = f"{e[0] / 100:+.2f}/{e[1] if e[1] is not None else '?'}"
    game.headers["Result"] = g["result"]
    if g.get("error"):
        err = jloads(g["error"], {})
        game.headers["Termination"] = f"{g['termination']} ({err.get('role')} {err.get('engine_id')})"
    exporter = chess.pgn.StringExporter(headers=True, variations=False, comments=True)
    return game.accept(exporter)


# ----------------------------------------------------------------------------- pages
@app.get("/")
def index():
    return send_from_directory(app.static_folder, "index.html")


# ----------------------------------------------------------------------------- system / settings
@app.get("/api/system")
def api_system():
    logical, phys = sysinfo.cpu_counts()
    g = S.load_global(db)
    return jsonify({
        "logical_cpus": logical, "physical_cpus": phys,
        "cpu_pool": sysinfo.cpu_pool(g), "auto_cpu_pool": sysinfo.auto_cpu_pool(),
        "engines_dir": str(store.root), "openings": book.summary(),
    })


@app.get("/api/settings")
def api_get_settings():
    g = S.load_global(db)
    return jsonify({"global": {k: g[k] for k in S.GLOBAL_SPEC}, "test_defaults": {k: g[k] for k in S.TEST_SPEC}})


@app.put("/api/settings")
def api_put_settings():
    S.save_global(db, body())
    return api_get_settings()


@app.post("/api/settings/test-webhook")
def api_test_webhook():
    url = body().get("url") or S.load_global(db)["discord_webhook_url"]
    ok, msg = sysinfo.send_discord(url, "SPRT tester", "Webhook test - notifications are working.", wait=True)
    return jsonify({"ok": ok, "message": msg}), (200 if ok else 400)


@app.post("/api/openings/rescan")
def api_rescan_openings():
    book.scan()
    return jsonify(book.summary())


# ----------------------------------------------------------------------------- engines
@app.get("/api/engines")
def api_engines():
    base_counts = {r["base_id"]: r["n"] for r in db.query("SELECT base_id, COUNT(*) AS n FROM tests GROUP BY base_id")}
    cand_counts = {r["cand_id"]: r["n"] for r in db.query("SELECT cand_id, COUNT(*) AS n FROM tests GROUP BY cand_id")}
    out = []
    for e in store.scan():
        e = dict(e)
        e["sha"] = store.sha(Path(e["exe_path"]))
        e["tests_as_base"] = base_counts.get(e["id"], 0)
        e["tests_as_candidate"] = cand_counts.get(e["id"], 0)
        del e["exe_path"]
        out.append(e)
    return jsonify(out)


@app.post("/api/engines")
def api_add_engine():
    f = request.files.get("exe")
    if f is None or not f.filename:
        raise ValueError("choose an executable to upload")
    form = request.form
    folder = (form.get("folder") or "").strip()
    if not folder:
        raise ValueError("folder name is required")
    eng = store.add(f, folder, (form.get("group") or "").strip().strip("/"), {
        "name": form.get("name"), "version": form.get("version"), "parent": form.get("parent"),
        "tags": form.get("tags"), "notes": form.get("notes"),
    })
    return jsonify({"id": eng["id"]}), 201


@app.patch("/api/engines/<path:eid>")
def api_patch_engine(eid):
    try:
        eng = store.update(eid, body())
    except KeyError:
        return jsonify({"error": "engine not found"}), 404
    return jsonify({"id": eng["id"]})


# ----------------------------------------------------------------------------- experiments
@app.get("/api/experiments")
def api_experiments():
    rows = db.query(
        "SELECT e.*, COUNT(t.id) AS tests FROM experiments e LEFT JOIN tests t ON t.experiment_id=e.id "
        "GROUP BY e.id ORDER BY e.id DESC")
    return jsonify(rows)


@app.post("/api/experiments")
def api_create_experiment():
    b = body()
    name = (b.get("name") or "").strip()
    if not name:
        raise ValueError("experiment name is required")
    if db.one("SELECT id FROM experiments WHERE name=?", (name,)):
        return jsonify({"error": "an experiment with this name already exists"}), 409
    eid = db.execute("INSERT INTO experiments(name, description, created_at) VALUES (?,?,?)",
                     (name, b.get("description", ""), time.time()))
    return jsonify({"id": eid}), 201


@app.patch("/api/experiments/<int:eid>")
def api_patch_experiment(eid):
    b = body()
    row = db.one("SELECT * FROM experiments WHERE id=?", (eid,))
    if not row:
        return jsonify({"error": "experiment not found"}), 404
    name = (b.get("name", row["name"]) or "").strip()
    if not name:
        raise ValueError("experiment name is required")
    db.execute("UPDATE experiments SET name=?, description=? WHERE id=?", (name, b.get("description", row["description"]), eid))
    return jsonify({"id": eid})


@app.get("/api/experiments/<int:eid>")
def api_experiment(eid):
    row = db.one("SELECT * FROM experiments WHERE id=?", (eid,))
    if not row:
        return jsonify({"error": "experiment not found"}), 404
    names = engine_names()
    tests = [test_dict(t, names, {eid: row["name"]}) for t in
             db.query("SELECT * FROM tests WHERE experiment_id=? ORDER BY id", (eid,))]
    return jsonify({**row, "tests": tests})


# ----------------------------------------------------------------------------- tests
@app.get("/api/tests")
def api_tests():
    q = request.args
    where, params = [], []
    if q.get("base"):
        where.append("base_id=?"); params.append(q["base"])
    if q.get("cand"):
        where.append("cand_id=?"); params.append(q["cand"])
    if q.get("experiment") == "none":
        where.append("experiment_id IS NULL")
    elif q.get("experiment"):
        where.append("experiment_id=?"); params.append(int(q["experiment"]))
    if q.get("status"):
        where.append("status=?"); params.append(q["status"])
    if q.get("verdict"):
        where.append("verdict=?"); params.append(q["verdict"])
    if q.get("q"):
        like = f"%{q['q']}%"
        where.append("(label LIKE ? OR notes LIKE ? OR base_id LIKE ? OR cand_id LIKE ?)")
        params += [like] * 4
    sql = "SELECT * FROM tests" + (" WHERE " + " AND ".join(where) if where else "") + " ORDER BY id DESC"
    names = engine_names()
    experiments = {r["id"]: r["name"] for r in db.query("SELECT id, name FROM experiments")}
    out = []
    for t in db.query(sql, params):
        d = test_dict(t, names, experiments)
        d["live"] = manager.live(t["id"]) if t["status"] in ("running", "pausing") else None
        out.append(d)
    return jsonify(out)


@app.post("/api/tests")
def api_create_test():
    b = body()
    exp_id = b.get("experiment_id") or None
    new_exp = (b.get("new_experiment") or "").strip()
    if new_exp:
        row = db.one("SELECT id FROM experiments WHERE name=?", (new_exp,))
        exp_id = row["id"] if row else db.execute(
            "INSERT INTO experiments(name, description, created_at) VALUES (?,?,?)", (new_exp, "", time.time()))
    tid = manager.create_test(b.get("base_id"), b.get("cand_id"), b.get("settings"), exp_id,
                              b.get("label", ""), b.get("notes", ""), bool(b.get("allow_duplicate")))
    if b.get("start"):
        manager.start_test(tid)
    return jsonify({"id": tid}), 201


@app.get("/api/tests/<int:tid>")
def api_test(tid):
    t = manager._row(tid)
    d = test_dict(t)
    d["live"] = manager.live(tid)
    d["openings"] = manager.opening_progress(tid)
    d["events"] = db.query("SELECT ts, kind, message FROM test_events WHERE test_id=? ORDER BY id DESC LIMIT 60", (tid,))
    changed = []
    if store.get(t["base_id"]) and store.sha_of(t["base_id"]) != t["base_sha"]:
        changed.append("base")
    if store.get(t["cand_id"]) and store.sha_of(t["cand_id"]) != t["cand_sha"]:
        changed.append("candidate")
    d["binary_changed"] = changed
    d["engines_missing"] = [x for x, e in (("base", t["base_id"]), ("candidate", t["cand_id"])) if not store.get(e)]
    return jsonify(d)


@app.patch("/api/tests/<int:tid>")
def api_patch_test(tid):
    manager.update_test(tid, body())
    return api_test(tid)


@app.delete("/api/tests/<int:tid>")
def api_delete_test(tid):
    manager.delete_test(tid)
    return jsonify({"ok": True})


@app.post("/api/tests/<int:tid>/start")
def api_start_test(tid):
    manager.start_test(tid, force=bool(body().get("force")))
    return api_test(tid)


@app.post("/api/tests/<int:tid>/pause")
def api_pause_test(tid):
    manager.pause_test(tid, now=bool(body().get("now")))
    return api_test(tid)


@app.post("/api/tests/<int:tid>/rescan-openings")
def api_test_rescan(tid):
    added, total = manager.sync_openings(tid)
    return jsonify({"added": added, "total": total})


@app.get("/api/tests/<int:tid>/games")
def api_test_games(tid):
    manager._row(tid)
    q = request.args
    where, params = ["test_id=?"], [tid]
    if q.get("outcome") in ("W", "D", "L"):
        where.append("cand_points=?"); params.append({"W": 1.0, "D": 0.5, "L": 0.0}[q["outcome"]])
    if q.get("termination"):
        where.append("termination=?"); params.append(q["termination"])
    if q.get("errors") == "1":
        where.append("error IS NOT NULL")
    if q.get("color") in ("white", "black"):
        where.append("cand_white=?"); params.append(1 if q["color"] == "white" else 0)
    if q.get("opening"):
        where.append("opening_name LIKE ?"); params.append(f"%{q['opening']}%")
    w = " AND ".join(where)
    limit = min(int(q.get("limit", 50)), 500)
    offset = max(int(q.get("offset", 0)), 0)
    total = db.one(f"SELECT COUNT(*) AS n FROM games WHERE {w}", params)["n"]
    rows = db.query(
        f"SELECT id, opening_name, cand_white, cand_points, result, termination, plies, duration_ms, "
        f"finished_at, error FROM games WHERE {w} ORDER BY id DESC LIMIT ? OFFSET ?", (*params, limit, offset))
    detail = q.get("errors") == "1"
    out = []
    for g in rows:
        row = game_row_light(g)
        if not detail:
            row["error"] = None  # full reports only in the error log view
        out.append(row)
    terms = [r["termination"] for r in db.query("SELECT DISTINCT termination FROM games WHERE test_id=?", (tid,))]
    return jsonify({"total": total, "games": out, "terminations": sorted(terms)})


@app.get("/api/games/<int:gid>")
def api_game(gid):
    g = db.one("SELECT * FROM games WHERE id=?", (gid,))
    if not g:
        return jsonify({"error": "game not found"}), 404
    t = manager._row(g["test_id"])
    names = engine_names()
    board = chess.Board(g["start_fen"]) if g["start_fen"] else chess.Board()
    start_turn = "w" if board.turn == chess.WHITE else "b"
    moves = jloads(g["moves"], [])
    evals = jloads(g["evals"], [])
    plies = []
    for i, u in enumerate(moves):
        mv = chess.Move.from_uci(u)
        san = board.san(mv)
        board.push(mv)
        plies.append({"uci": u, "san": san, "fen": board.fen(), "eval": evals[i] if i < len(evals) else None})
    cand_name = names.get(t["cand_id"], t["cand_id"])
    base_name = names.get(t["base_id"], t["base_id"])
    return jsonify({
        "id": g["id"], "test_id": g["test_id"], "opening_name": g["opening_name"],
        "start_fen": g["start_fen"], "opening_plies": g["opening_plies"], "plies": plies,
        "result": g["result"], "termination": g["termination"], "cand_white": bool(g["cand_white"]),
        "white": {"name": cand_name if g["cand_white"] else base_name, "role": "candidate" if g["cand_white"] else "base"},
        "black": {"name": base_name if g["cand_white"] else cand_name, "role": "base" if g["cand_white"] else "candidate"},
        "error": jloads(g["error"]), "duration_ms": g["duration_ms"],
        "start_turn": start_turn,
    })


@app.get("/api/games/<int:gid>/pgn")
def api_game_pgn(gid):
    g = db.one("SELECT * FROM games WHERE id=?", (gid,))
    if not g:
        return jsonify({"error": "game not found"}), 404
    t = manager._row(g["test_id"])
    return Response(build_pgn(g, t, engine_names()), mimetype="application/x-chess-pgn",
                    headers={"Content-Disposition": f"attachment; filename=game-{gid}.pgn"})


@app.get("/api/tests/<int:tid>/pgn")
def api_test_pgn(tid):
    t = manager._row(tid)
    names = engine_names()
    parts = [build_pgn(g, t, names) for g in db.query("SELECT * FROM games WHERE test_id=? ORDER BY id", (tid,))]
    return Response("\n\n".join(parts), mimetype="application/x-chess-pgn",
                    headers={"Content-Disposition": f"attachment; filename=test-{tid}.pgn"})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5000)
    args = ap.parse_args()
    logical, phys = sysinfo.cpu_counts()
    print(f"SPRT tester: http://{args.host}:{args.port}   ({logical} logical / {phys} physical CPUs)")
    print(f"engines: {len(store.scan())} found   openings: {len(book.hashes())} valid ({book.summary()['invalid_count']} invalid)")
    try:
        app.run(host=args.host, port=args.port, threaded=True, use_reloader=False)
    finally:
        manager.shutdown()


if __name__ == "__main__":
    main()
