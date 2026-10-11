"""Finds engines in engines/ and keeps their metadata.

Layout (any depth up to 4, folders are free-form so you can group by feature):

    engines/
      v001-baseline/
        engine.exe
        engine.json          <- optional: name, version, parent, tags, notes
      doubled-pawn/
        v012-dp-05/engine.exe
        v012-dp-10/engine.exe

An engine's id is its folder path relative to engines/ (e.g. "doubled-pawn/v012-dp-05").
A folder is an engine when it directly contains an executable.
"""
import hashlib
import json
import os
import re
import threading
from pathlib import Path

SAFE_PART = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
META_FIELDS = ("name", "version", "parent", "tags", "notes")
MAX_DEPTH = 4


def find_executable(folder: Path, hint=None):
    if hint and (folder / hint).is_file():
        return folder / hint
    for name in ("engine.exe", "engine"):
        p = folder / name
        if p.is_file():
            return p
    exes = sorted(p for p in folder.iterdir() if p.is_file() and p.suffix.lower() == ".exe")
    if exes:
        return exes[0]
    if os.name != "nt":
        for p in sorted(folder.iterdir()):
            if p.is_file() and os.access(p, os.X_OK) and p.suffix.lower() not in (".json", ".md", ".txt"):
                return p
    return None


def _read_meta(folder: Path):
    p = folder / "engine.json"
    if p.is_file():
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                return data
        except (ValueError, OSError):
            pass
    return {}


class EngineStore:
    def __init__(self, root):
        self.root = Path(root)
        self.root.mkdir(exist_ok=True)
        self._sha_cache = {}
        self._lock = threading.Lock()

    # ---- hashing -----------------------------------------------------------
    def sha(self, exe: Path):
        st = exe.stat()
        key = (str(exe), st.st_mtime_ns, st.st_size)
        with self._lock:
            cached = self._sha_cache.get(key)
        if cached:
            return cached
        h = hashlib.sha256()
        with open(exe, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        digest = h.hexdigest()[:16]
        with self._lock:
            self._sha_cache[key] = digest
        return digest

    # ---- scanning ----------------------------------------------------------
    def scan(self):
        engines = []

        def walk(folder: Path, depth):
            try:
                entries = sorted(p for p in folder.iterdir() if p.is_dir() and not p.name.startswith("."))
            except OSError:
                return
            for d in entries:
                meta = _read_meta(d)
                exe = find_executable(d, meta.get("exe"))
                if exe is not None:
                    engines.append(self._describe(d, exe, meta))
                elif depth < MAX_DEPTH:
                    walk(d, depth + 1)

        walk(self.root, 1)
        return engines

    def _describe(self, folder: Path, exe: Path, meta):
        eid = folder.relative_to(self.root).as_posix()
        tags = meta.get("tags", [])
        if isinstance(tags, str):
            tags = [t.strip() for t in tags.split(",") if t.strip()]
        return {
            "id": eid,
            "name": meta.get("name") or folder.name,
            "version": meta.get("version", ""),
            "parent": meta.get("parent", ""),
            "tags": tags,
            "notes": meta.get("notes", ""),
            "exe_name": exe.name,
            "exe_path": str(exe),
            "size": exe.stat().st_size,
            "modified": exe.stat().st_mtime,
            "group": folder.parent.relative_to(self.root).as_posix() if folder.parent != self.root else "",
        }

    def get(self, eid):
        folder = self._folder(eid)
        if folder is None or not folder.is_dir():
            return None
        meta = _read_meta(folder)
        exe = find_executable(folder, meta.get("exe"))
        return self._describe(folder, exe, meta) if exe else None

    def sha_of(self, eid):
        eng = self.get(eid)
        return self.sha(Path(eng["exe_path"])) if eng else None

    def _folder(self, eid):
        parts = [p for p in eid.split("/") if p]
        if not parts or any(not SAFE_PART.match(p) for p in parts):
            return None
        return self.root.joinpath(*parts)

    # ---- editing -----------------------------------------------------------
    def update(self, eid, fields):
        folder = self._folder(eid)
        if folder is None or not folder.is_dir():
            raise KeyError(eid)
        meta = _read_meta(folder)
        for k in META_FIELDS:
            if k in fields:
                v = fields[k]
                if k == "tags" and isinstance(v, str):
                    v = [t.strip() for t in v.split(",") if t.strip()]
                meta[k] = v
        (folder / "engine.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
        return self.get(eid)

    def add(self, file_storage, folder_name, group="", fields=None):
        """Save an uploaded executable into engines/<group>/<folder_name>/ plus engine.json."""
        parts = [p for p in (group or "").split("/") if p] + [folder_name]
        for p in parts:
            if not SAFE_PART.match(p):
                raise ValueError(f"folder name {p!r} may only use letters, digits, '.', '_' and '-'")
        folder = self.root.joinpath(*parts)
        if folder.exists():
            raise ValueError(f"engines/{'/'.join(parts)} already exists")
        filename = file_storage.filename or "engine.exe"
        filename = re.sub(r"[^A-Za-z0-9._-]", "_", os.path.basename(filename)) or "engine.exe"
        folder.mkdir(parents=True)
        file_storage.save(str(folder / filename))
        try:
            os.chmod(folder / filename, 0o755)
        except OSError:
            pass
        meta = {"exe": filename}
        for k in META_FIELDS:
            if fields and k in fields and fields[k] not in (None, ""):
                v = fields[k]
                if k == "tags" and isinstance(v, str):
                    v = [t.strip() for t in v.split(",") if t.strip()]
                meta[k] = v
        (folder / "engine.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
        return self.get("/".join(parts))
