"""Setting definitions, validation and persistence.

There are two groups:
  * TEST_SPEC   - per-test settings. The values on the Settings page are the defaults
                  copied into every new test; each test can then edit its own copy.
  * GLOBAL_SPEC - settings that apply to the whole app (workers, CPU pinning, Discord).
"""
import json

# key: (type, min, max, default)   type is int / float / bool / str / ("choice", [...])
TEST_SPEC = {
    "movetime_ms": (int, 1, 600000, 100),
    "band_elo": (float, 0.1, 100.0, 5.0),
    "alpha": (float, 0.0001, 0.5, 0.05),
    "beta": (float, 0.0001, 0.5, 0.05),
    "max_games": (int, 2, 100000000, 10000),
    "min_games": (int, 0, 100000000, 100),
    "auto_stop": (bool, None, None, True),
    "adj_win_enabled": (bool, None, None, False),
    "adj_win_cp": (int, 50, 30000, 1000),
    "adj_win_moves": (int, 1, 200, 8),
    "adj_draw_enabled": (bool, None, None, False),
    "adj_draw_cp": (int, 0, 1000, 10),
    "adj_draw_moves": (int, 1, 200, 10),
    "adj_draw_min_move": (int, 0, 500, 30),
    "hard_timeout_factor": (float, 1.5, 1000.0, 10.0),
    "hard_timeout_min_ms": (int, 100, 600000, 2000),
    "startup_grace_ms": (int, 0, 600000, 2000),
    "error_abort_limit": (int, 0, 100000, 10),
    "max_plies": (int, 20, 5000, 600),
    "score_perspective": (("choice", ["white", "stm"]), None, None, "white"),
}

GLOBAL_SPEC = {
    "workers": (int, 1, 512, 16),
    "pin_cpus": (bool, None, None, True),
    "cpu_list": (str, None, None, ""),
    "discord_webhook_url": (str, None, None, ""),
    "notify_finish": (bool, None, None, True),
    "notify_abort": (bool, None, None, True),
    "public_url": (str, None, None, "http://127.0.0.1:5000"),
}

ALL_SPEC = {**TEST_SPEC, **GLOBAL_SPEC}


def _coerce(key, spec, value):
    typ, lo, hi, _default = spec
    if isinstance(typ, tuple):
        if value not in typ[1]:
            raise ValueError(f"{key} must be one of {typ[1]}")
        return value
    if typ is bool:
        if isinstance(value, str):
            return value.strip().lower() in ("1", "true", "yes", "on")
        return bool(value)
    if typ is str:
        return "" if value is None else str(value).strip()
    try:
        v = typ(value)
    except (TypeError, ValueError):
        raise ValueError(f"{key} must be a number")
    if lo is not None and v < lo:
        raise ValueError(f"{key} must be at least {lo}")
    if hi is not None and v > hi:
        raise ValueError(f"{key} must be at most {hi}")
    return v


def clean(spec_map, values, partial=True):
    """Validate and coerce `values` against spec_map. Unknown keys are ignored."""
    out = {}
    for key, spec in spec_map.items():
        if key in values:
            out[key] = _coerce(key, spec, values[key])
        elif not partial:
            out[key] = spec[3]
    return out


def defaults(spec_map):
    return {k: v[3] for k, v in spec_map.items()}


def load_global(db):
    stored = {r["key"]: json.loads(r["value"]) for r in db.query("SELECT key, value FROM settings")}
    merged = defaults(ALL_SPEC)
    for k, v in stored.items():
        if k in ALL_SPEC:
            try:
                merged[k] = _coerce(k, ALL_SPEC[k], v)
            except ValueError:
                pass
    return merged


def save_global(db, values):
    cleaned = clean(ALL_SPEC, values)
    with db.transaction():
        for k, v in cleaned.items():
            db.execute(
                "INSERT INTO settings(key, value) VALUES(?, ?) "
                "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                (k, json.dumps(v)),
            )
    return cleaned


def test_defaults(db):
    g = load_global(db)
    return {k: g[k] for k in TEST_SPEC}


def with_test_defaults(test_settings):
    """Fill any missing keys of a stored per-test settings dict."""
    out = defaults(TEST_SPEC)
    out.update({k: v for k, v in test_settings.items() if k in TEST_SPEC})
    return out
