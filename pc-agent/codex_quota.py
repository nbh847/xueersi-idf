"""Codex quota adapter: read-only App Server client plus normalization.

The App Server is started as a local child process
(``codex app-server --listen stdio://``) and only queried, never asked to
refresh or change credentials. Nothing here reads, stores or prints the
Codex login material: the CLI owns it, the stderr drain discards it and
only whitelisted quota fields leave this module (goal decisions 4 and 6).
"""

from __future__ import annotations

import json
import shutil
import subprocess
import threading
import time
from typing import Callable, Dict, List, Optional

from quota_cache import (
    ERROR_AUTH_REQUIRED,
    ERROR_INVALID_DATA,
    ERROR_UNAVAILABLE,
    POLL_INTERVAL_SEC,
    STATUS_AUTH_REQUIRED,
    STATUS_INVALID_DATA,
    STATUS_UNAVAILABLE,
    QuotaCache,
    QuotaError,
    finite_number,
    remaining_percent,
    valid_timestamp_seconds,
)

PROVIDER_ID = "codex"

# Frozen 2026-09-15 mapping: 300 minutes is the 5 hour window and 10080
# minutes the weekly one. Any other duration is not one of the two target
# windows and is dropped instead of guessed.
FIXED_WINDOW_LABELS = {300: "5H", 10080: "7D"}
WINDOW_ORDER = ("5H", "7D")

CLIENT_NAME = "xiaomiao-agent"
CLIENT_VERSION = "0.1.0"
HANDSHAKE_TIMEOUT_SEC = 30.0
REQUEST_TIMEOUT_SEC = 30.0

# Finite backoff for rebuilding a dead App Server child; the scheduler
# restarts on the last step and recovers without a user re-login.
RESTART_BACKOFF_SEC = (1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 60.0)


class AppServerError(Exception):
    """The App Server answered with a JSON-RPC error."""


class AppServerTimeout(AppServerError):
    """No matching response arrived before the timeout."""


class AppServerUnavailable(AppServerError):
    """Child process missing, not running, or stdin write failed."""


def resolve_command() -> List[str]:
    """Locate the Codex CLI entry point.

    ``CreateProcess`` does not resolve ``.cmd`` shims, so the npm-installed
    ``codex.cmd`` on Windows is launched through ``cmd /c``.
    """
    command = shutil.which("codex.cmd") or shutil.which("codex")
    if command is None:
        raise AppServerUnavailable("codex executable not found in PATH")
    if command.lower().endswith(".cmd"):
        return ["cmd", "/c", command]
    return [command]


class AppServerClient:
    """JSON-RPC client for one App Server child process.

    The reader thread owns stdout and dispatches responses by id and
    notifications by method; ``request()`` may be called from several
    threads while stdin writes stay serialized. When the process exits all
    waiters are woken and the client is marked unavailable; the scheduler
    owns rebuilding it.
    """

    def __init__(
        self,
        spawner: Optional[Callable[[], subprocess.Popen]] = None,
        request_timeout: float = REQUEST_TIMEOUT_SEC,
        handshake_timeout: float = HANDSHAKE_TIMEOUT_SEC,
    ) -> None:
        self._spawner = spawner or self._default_spawn
        self._request_timeout = request_timeout
        self._handshake_timeout = handshake_timeout
        self._proc: Optional[subprocess.Popen] = None
        self._reader: Optional[threading.Thread] = None
        self._stderr_reader: Optional[threading.Thread] = None
        self._state_lock = threading.Lock()
        self._write_lock = threading.Lock()
        self._next_id = 1
        self._pending: Dict[int, Dict] = {}
        self._notification_handlers: List[Callable[[str, object], None]] = []
        self._stopped = False

    @staticmethod
    def _default_spawn() -> subprocess.Popen:
        return subprocess.Popen(
            resolve_command() + ["app-server", "--listen", "stdio://"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
        )

    def add_notification_handler(self, handler: Callable[[str, object], None]) -> None:
        """Register ``handler(method, params)``; handler errors are swallowed."""
        self._notification_handlers.append(handler)

    def start(self) -> None:
        """Spawn the child and finish the initialize/initialized handshake."""
        with self._state_lock:
            if self._proc is not None:
                raise AppServerUnavailable("client already started")
        try:
            proc = self._spawner()
        except OSError as error:
            raise AppServerUnavailable("failed to spawn app server") from error
        with self._state_lock:
            self._proc = proc
            self._reader = threading.Thread(
                target=self._reader_loop, args=(proc,), name="app-server-reader", daemon=True
            )
            self._stderr_reader = threading.Thread(
                target=self._stderr_loop, args=(proc,), name="app-server-stderr", daemon=True
            )
            self._reader.start()
            self._stderr_reader.start()
        try:
            self.request(
                "initialize",
                {
                    "clientInfo": {
                        "name": CLIENT_NAME,
                        "title": "Xiaomiao Agent",
                        "version": CLIENT_VERSION,
                    }
                },
                timeout=self._handshake_timeout,
            )
            self.notify("initialized", {})
        except Exception:
            self.stop()
            raise

    def stop(self) -> None:
        """Wake waiters and terminate the child without leaving it behind."""
        self._stopped = True
        self._fail_pending()
        with self._state_lock:
            proc, self._proc = self._proc, None
        if proc is None:
            return
        try:
            proc.terminate()
        except OSError:
            pass
        try:
            proc.wait(timeout=5)
        except Exception:
            try:
                proc.kill()
            except OSError:
                pass
            try:
                proc.wait(timeout=5)
            except Exception:
                pass
        if proc.stdin and not proc.stdin.closed:
            try:
                proc.stdin.close()
            except OSError:
                pass

    @property
    def running(self) -> bool:
        with self._state_lock:
            proc = self._proc
        return proc is not None and proc.poll() is None

    def request(self, method: str, params: Optional[Dict] = None, timeout: Optional[float] = None):
        """Send one request and return its result; errors raise AppServerError."""
        with self._state_lock:
            if self._stopped or self._proc is None:
                raise AppServerUnavailable("app server not running")
            request_id = self._next_id
            self._next_id += 1
            slot = {"event": threading.Event(), "response": None, "dead": False}
            self._pending[request_id] = slot

        line = json.dumps(
            {
                "jsonrpc": "2.0",
                "id": request_id,
                "method": method,
                "params": params if params is not None else {},
            }
        )
        try:
            with self._write_lock:
                proc = self._proc
                if proc is None:
                    raise AppServerUnavailable("app server stopped before write")
                proc.stdin.write(line + "\n")
                proc.stdin.flush()
        except (OSError, ValueError) as error:
            self._pending.pop(request_id, None)
            raise AppServerUnavailable("failed to write request") from error

        wait = self._request_timeout if timeout is None else timeout
        if not slot["event"].wait(wait):
            self._pending.pop(request_id, None)
            raise AppServerTimeout("request timed out")
        if slot["dead"]:
            self._pending.pop(request_id, None)
            raise AppServerUnavailable("app server exited before response")
        response = slot["response"]
        self._pending.pop(request_id, None)
        if response is None:
            raise AppServerUnavailable("app server response missing")
        if "error" in response:
            # Only the JSON-RPC message text is passed on; the raw object
            # may carry upstream details and is dropped here.
            raise AppServerError(str(response["error"].get("message", "unknown error")))
        return response.get("result")

    def notify(self, method: str, params: Optional[Dict] = None) -> None:
        """Send a JSON-RPC notification; failures are ignored on purpose."""
        with self._state_lock:
            if self._stopped:
                return
            proc = self._proc
        if proc is None:
            return
        line = json.dumps(
            {"jsonrpc": "2.0", "method": method, "params": params if params is not None else {}}
        )
        try:
            with self._write_lock:
                proc.stdin.write(line + "\n")
                proc.stdin.flush()
        except (OSError, ValueError):
            pass

    def _reader_loop(self, proc: subprocess.Popen) -> None:
        try:
            for line in proc.stdout:
                text = line.strip()
                if not text:
                    continue
                try:
                    message = json.loads(text)
                except ValueError:
                    # Diagnostic text mixed into the stream is skipped.
                    continue
                self._dispatch(message)
        except (OSError, ValueError):
            pass
        finally:
            self._fail_pending()

    @staticmethod
    def _stderr_loop(proc: subprocess.Popen) -> None:
        """Drain stderr so the child cannot block; contents are never kept."""
        try:
            for _ in proc.stderr:
                pass
        except (OSError, ValueError):
            pass

    def _dispatch(self, message) -> None:
        if not isinstance(message, dict):
            return
        request_id = message.get("id")
        if request_id is not None:
            with self._state_lock:
                slot = self._pending.get(request_id)
            if slot is None:
                return
            slot["response"] = message
            slot["event"].set()
            return
        method = message.get("method")
        if method is None:
            return
        params = message.get("params")
        for handler in list(self._notification_handlers):
            try:
                handler(method, params)
            except Exception:
                continue

    def _fail_pending(self) -> None:
        with self._state_lock:
            slots = list(self._pending.values())
        for slot in slots:
            slot["dead"] = True
            slot["event"].set()


def normalize_window(raw_window) -> Optional[Dict]:
    """Normalize one Codex window; unknown duration or used value is dropped."""
    if not isinstance(raw_window, dict):
        return None
    duration = raw_window.get("windowDurationMins")
    if isinstance(duration, bool) or not isinstance(duration, int):
        return None
    label = FIXED_WINDOW_LABELS.get(duration)
    if label is None:
        return None
    remaining = remaining_percent(raw_window.get("usedPercent"))
    if remaining is None:
        return None
    return {
        "label": label,
        "remaining_percent": remaining,
        "resets_at": valid_timestamp_seconds(raw_window.get("resetsAt")),
    }


def normalize_rate_limits(raw) -> Dict:
    """Normalize ``account/rateLimits/read`` into the two target windows.

    Only ``rateLimitsByLimitId.codex`` primary/secondary are read. Both
    being invalid raises ``invalid_data``; a single valid window is kept
    and the display layer degrades the missing one.
    """
    by_limit = raw.get("rateLimitsByLimitId") if isinstance(raw, dict) else None
    codex = by_limit.get("codex") if isinstance(by_limit, dict) else None
    if not isinstance(codex, dict):
        raise QuotaError(STATUS_INVALID_DATA, ERROR_INVALID_DATA)

    plan = codex.get("planType")
    if not isinstance(plan, str) or not plan:
        plan = None

    found: Dict[str, Dict] = {}
    for name in ("primary", "secondary"):
        window = normalize_window(codex.get(name))
        if window is not None:
            found[window["label"]] = window
    windows = [found[label] for label in WINDOW_ORDER if label in found]
    if not windows:
        raise QuotaError(STATUS_INVALID_DATA, ERROR_INVALID_DATA)
    return {"plan": plan, "windows": windows}


class CodexWorker:
    """Owns the App Server lifecycle and feeds one :class:`QuotaCache`."""

    def __init__(
        self,
        cache: QuotaCache,
        client_factory: Optional[Callable[[], AppServerClient]] = None,
        poll_interval: float = POLL_INTERVAL_SEC,
    ) -> None:
        self._cache = cache
        self._client_factory = client_factory or AppServerClient
        self._poll_interval = poll_interval
        self._wake = threading.Event()
        self._stop_flag = False
        self._auth_confirmed = False
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(target=self._run_loop, name="codex-quota", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_flag = True
        self._wake.set()
        if self._thread is not None:
            self._thread.join(timeout=max(self._poll_interval * 2, 2.0))
            self._thread = None

    def _run_loop(self) -> None:
        backoff_index = 0
        client: Optional[AppServerClient] = None
        while not self._stop_flag:
            if client is None or not client.running:
                if client is not None:
                    client.stop()
                    client = None
                try:
                    client = self._restart_client(backoff_index)
                except AppServerError:
                    backoff_index += 1
                    continue
                if client is None:
                    break
                backoff_index += 1
            try:
                if self._update_once(client):
                    backoff_index = 0
            except AppServerUnavailable:
                client.stop()
                client = None
                continue
            self._wake.wait(self._poll_interval)
            self._wake.clear()
        if client is not None:
            client.stop()

    def _restart_client(self, backoff_index: int) -> Optional[AppServerClient]:
        """Wait on the finite backoff, then spawn and handshake one client."""
        delay = RESTART_BACKOFF_SEC[min(backoff_index, len(RESTART_BACKOFF_SEC) - 1)]
        self._cache.set_failure(STATUS_UNAVAILABLE, ERROR_UNAVAILABLE)
        self._wake.wait(delay)
        self._wake.clear()
        if self._stop_flag:
            return None
        client = self._client_factory()
        client.add_notification_handler(self._handle_notification)
        try:
            client.start()
        except Exception:
            client.stop()
            raise
        return client

    def _handle_notification(self, method: str, params) -> None:
        if method == "account/rateLimits/updated":
            self._wake.set()
        elif method == "account/updated":
            self._auth_confirmed = False
            self._wake.set()

    def _update_once(self, client: AppServerClient) -> bool:
        """Run one auth check and quota read; True when the cache was updated."""
        if not self._auth_confirmed:
            try:
                self._check_auth(client)
            except AppServerUnavailable:
                # Process is gone: the scheduler must rebuild it.
                raise
            except AppServerError:
                return False
            if not self._auth_confirmed:
                # Without a ChatGPT login there is no quota to read, and a
                # later read must not overwrite auth_required.
                return False
        try:
            raw = client.request("account/rateLimits/read")
        except AppServerUnavailable:
            self._cache.set_failure(STATUS_UNAVAILABLE, ERROR_UNAVAILABLE)
            raise
        except AppServerError:
            self._cache.set_failure(STATUS_UNAVAILABLE, ERROR_UNAVAILABLE)
            return False
        try:
            normalized = normalize_rate_limits(raw)
        except QuotaError:
            self._cache.set_failure(STATUS_INVALID_DATA, ERROR_INVALID_DATA)
            return False
        self._cache.store(normalized["plan"], normalized["windows"])
        return True

    def _check_auth(self, client: AppServerClient) -> None:
        """Confirm the ChatGPT login; read-only and never refreshes the token."""
        try:
            # The current App Server protocol needs an explicit refreshToken
            # flag: omitting it can return account=null even with a valid
            # ChatGPT login. This is a read-only probe, not a refresh.
            result = client.request("account/read", {"refreshToken": False})
        except AppServerError:
            self._auth_confirmed = False
            self._cache.set_failure(STATUS_UNAVAILABLE, ERROR_UNAVAILABLE)
            raise
        account = result.get("account") if isinstance(result, dict) else None
        account_type = account.get("type") if isinstance(account, dict) else None
        if account_type == "chatgpt":
            self._auth_confirmed = True
            return
        self._auth_confirmed = False
        self._cache.set_failure(STATUS_AUTH_REQUIRED, ERROR_AUTH_REQUIRED)
