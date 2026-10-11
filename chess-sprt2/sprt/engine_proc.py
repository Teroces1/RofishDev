"""A tiny wrapper around one engine process.

A reader thread pushes every stdout/stderr line into a queue, so the game loop can wait
for `bestmove` with a hard timeout (which a plain blocking readline() can't do on Windows).
"""
import atexit
import os
import queue
import subprocess
import threading
import time
from collections import deque

try:
    import psutil
except ImportError:  # pinning is optional
    psutil = None

EOF = object()

_LIVE = set()
_LIVE_LOCK = threading.Lock()


def kill_all():
    """Safety net: never leave engine processes behind when the server exits."""
    with _LIVE_LOCK:
        procs = list(_LIVE)
    for p in procs:
        try:
            p.proc.kill()
        except Exception:
            pass


atexit.register(kill_all)


class EngineProcess:
    def __init__(self, path, label, cpu=None):
        self.path = str(path)
        self.label = label
        self.lines = queue.Queue()
        self.tail = deque(maxlen=40)
        self.exit_code = None
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
        self.proc = subprocess.Popen(
            [self.path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,  # crash messages end up in the same tail
            cwd=os.path.dirname(self.path) or None,
            bufsize=0,
            creationflags=flags,
        )
        with _LIVE_LOCK:
            _LIVE.add(self)
        self.pinned = False
        if cpu is not None and psutil is not None:
            try:
                psutil.Process(self.proc.pid).cpu_affinity([cpu])
                self.pinned = True
            except Exception:
                pass
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _read(self):
        try:
            for raw in iter(self.proc.stdout.readline, b""):
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                self.tail.append(line)
                self.lines.put(line)
        except Exception:
            pass
        finally:
            self.lines.put(EOF)

    def alive(self):
        return self.proc.poll() is None

    def send(self, text):
        """Returns False if the pipe is broken (engine already dead)."""
        try:
            self.proc.stdin.write((text + "\n").encode())
            self.proc.stdin.flush()
            return True
        except (BrokenPipeError, OSError, ValueError):
            return False

    def drain(self):
        """Throw away stale lines (e.g. trailing output from the previous search)."""
        try:
            while True:
                item = self.lines.get_nowait()
                if item is EOF:
                    self.lines.put(EOF)
                    return
        except queue.Empty:
            return

    def wait_bestmove(self, timeout_s, stop_event=None):
        """Collect lines until 'bestmove'. Returns (kind, bestmove_line, info_lines, elapsed_ms).

        kind is one of: 'bestmove', 'eof' (process died), 'timeout', 'stopped'.
        """
        start = time.monotonic()
        deadline = start + timeout_s
        infos = []
        while True:
            if stop_event is not None and stop_event.is_set():
                return "stopped", None, infos, (time.monotonic() - start) * 1000
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return "timeout", None, infos, (time.monotonic() - start) * 1000
            try:
                item = self.lines.get(timeout=min(0.05, remaining))
            except queue.Empty:
                continue
            if item is EOF:
                self.lines.put(EOF)
                return "eof", None, infos, (time.monotonic() - start) * 1000

            cleaned = item.strip()
            if "bestmove" in cleaned:
                cleaned = cleaned[cleaned.find("bestmove"):]
                return "bestmove", cleaned, infos, (time.monotonic() - start) * 1000
            if cleaned.startswith("info"):
                infos.append(item)

    def close(self):
        """Polite shutdown: send quit, wait briefly, then kill."""
        if self.alive():
            self.send("quit")
            try:
                self.proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                self.kill()
        self.exit_code = self.proc.poll()
        self._cleanup()

    def kill(self):
        try:
            self.proc.kill()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            pass
        self.exit_code = self.proc.poll()
        self._cleanup()

    def _cleanup(self):
        with _LIVE_LOCK:
            _LIVE.discard(self)
        for f in (self.proc.stdin, self.proc.stdout):
            try:
                f.close()
            except Exception:
                pass
