"""CPU pool selection for pinning games, and Discord notifications."""
import json
import os
import threading
import urllib.request

try:
    import psutil
except ImportError:
    psutil = None


def cpu_counts():
    logical = os.cpu_count() or 1
    phys = logical
    if psutil is not None:
        phys = psutil.cpu_count(logical=False) or logical
    return logical, phys


def auto_cpu_pool():
    """One logical processor per physical core.

    * No SMT (logical == physical): every id.
    * SMT everywhere: every second id (0, 2, 4, ...).
    * Hybrid Intel (e.g. 20 cores / 28 threads): P-cores come first with 2 threads each,
      followed by single-threaded E-cores, so we take every second id of the P-core
      range and then all E-core ids. Override with the CPU list setting if your
      layout differs.
    """
    logical, phys = cpu_counts()
    if phys >= logical:
        return list(range(logical))
    if logical == 2 * phys:
        return list(range(0, logical, 2))
    smt_cores = logical - phys
    p_ids = list(range(0, 2 * smt_cores, 2))
    e_ids = list(range(2 * smt_cores, logical))
    return p_ids + e_ids


def parse_cpu_list(text, logical):
    """'0,2,4-7' -> [0, 2, 4, 5, 6, 7] (ids outside the machine are dropped)."""
    out = []
    for part in (text or "").replace(" ", "").split(","):
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            out.extend(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    seen, ids = set(), []
    for c in out:
        if 0 <= c < logical and c not in seen:
            seen.add(c)
            ids.append(c)
    return ids


def cpu_pool(settings):
    if not settings.get("pin_cpus", True):
        return []
    logical, _ = cpu_counts()
    if settings.get("cpu_list"):
        try:
            ids = parse_cpu_list(settings["cpu_list"], logical)
            if ids:
                return ids
        except ValueError:
            pass
    return auto_cpu_pool()


def send_discord(webhook_url, title, description, color=0x5865F2, fields=None, wait=False):
    """POST an embed to a Discord webhook. Runs in a thread unless wait=True.

    Returns (ok, message) when wait=True.
    """
    if not webhook_url:
        return (False, "no webhook URL set") if wait else None
    payload = {
        "username": "SPRT tester",
        "embeds": [{
            "title": title[:250],
            "description": description[:3500],
            "color": color,
            "fields": [{"name": n, "value": str(v)[:900], "inline": True} for n, v in (fields or [])][:20],
        }],
    }

    def _post():
        req = urllib.request.Request(
            webhook_url,
            data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json", "User-Agent": "sprt-tester/1.0"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                return True, f"HTTP {r.status}"
        except Exception as e:  # network errors must never break a test
            return False, str(e)

    if wait:
        return _post()
    threading.Thread(target=_post, daemon=True).start()
    return None
