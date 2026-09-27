"""Shared quota snapshot cache for the Xiaomiao Agent (AI quota pages goal).

Each AI provider (Codex, Zhipu) owns one :class:`QuotaCache` with its own
lock, snapshot and status, so a failure or a stalled scheduler on one side
never touches the other. HTTP worker threads only read a rendered copy;
upstream queries never run on the request path (same contract as the PC
metrics collector).

Frozen API v1 contract for the quota routes (goal 2026-09-27, decision 2):

- Outer ``schema_version`` is 1 and ``status`` is one of ``ok``,
  ``auth_required``, ``unavailable``, ``invalid_data`` or ``stale``.
- ``error_code`` is a short generic token and never carries upstream text,
  credentials or request details.
- ``data`` holds only ``provider_id``, nullable ``plan``, the target
  ``windows`` and ``updated_at_epoch``. Each window holds only ``label``,
  nullable ``remaining_percent``, nullable ``resets_at``, nullable
  ``reset_at_local`` and nullable ``reset_in_sec``.
- ``reset_at_local`` and ``reset_in_sec`` come from one local time sample
  per response; the device counts the remaining seconds down itself and
  never needs its own wall clock (decision 3).
"""

from __future__ import annotations

import math
import threading
import time
from typing import Callable, Dict, List, Optional

SCHEMA_VERSION = 1

STATUS_OK = "ok"
STATUS_AUTH_REQUIRED = "auth_required"
STATUS_UNAVAILABLE = "unavailable"
STATUS_INVALID_DATA = "invalid_data"
STATUS_STALE = "stale"

ERROR_OK = None
ERROR_AUTH_REQUIRED = "auth_required"
ERROR_UNAVAILABLE = "provider_unavailable"
ERROR_INVALID_DATA = "provider_data_invalid"
ERROR_STALE = "stale_snapshot"
ERROR_NO_SNAPSHOT = "no_snapshot"

# Quota windows refresh far slower than PC metrics: 60 s polling and a
# 180 s freshness window (goal decision 6). The device applies the same
# 180 s rule to its own copy; the 3 s PC metrics rule never applies here.
POLL_INTERVAL_SEC = 60.0
STALE_AFTER_SEC = 180.0

RESET_TIME_FORMAT = "%m-%d %H:%M"


class QuotaError(Exception):
    """One provider round failed; carries the API v1 status and error code."""

    def __init__(self, status: str, error_code: str) -> None:
        super().__init__(error_code)
        self.status = status
        self.error_code = error_code


def finite_number(value) -> Optional[float]:
    """Return ``value`` as a float when it is a finite real number.

    ``bool`` is an ``int`` subclass in Python, so it is rejected explicitly.
    """
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    number = float(value)
    if not math.isfinite(number):
        return None
    return number


def remaining_percent(used_percent) -> Optional[int]:
    """``clamp(round(100 - used), 0, 100)``; None when ``used`` is not a number.

    A real ``0`` (fully used up) stays ``0`` and is never turned into a
    missing value (goal decision 3).
    """
    used = finite_number(used_percent)
    if used is None:
        return None
    return int(max(0, min(100, round(100.0 - used))))


def valid_timestamp_seconds(value) -> Optional[int]:
    """Accept a positive integer Unix second; anything else is unknown."""
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    if value <= 0:
        return None
    return value


def format_local_time(epoch_sec: Optional[int]) -> Optional[str]:
    """Local ``MM-DD HH:MM`` for a reset timestamp, or None when unknown."""
    if epoch_sec is None:
        return None
    try:
        return time.strftime(RESET_TIME_FORMAT, time.localtime(epoch_sec))
    except (OverflowError, OSError, ValueError):
        return None


def window_payload(
    label: str,
    remaining: Optional[int],
    resets_at: Optional[int],
    now_epoch: int,
) -> Dict:
    """Render one window with the whitelisted fields of the frozen contract."""
    return {
        "label": label,
        "remaining_percent": remaining,
        "resets_at": resets_at,
        "reset_at_local": format_local_time(resets_at),
        "reset_in_sec": (
            max(resets_at - now_epoch, 0) if resets_at is not None else None
        ),
    }


class QuotaCache:
    """Thread-safe snapshot for one provider, with its own lock and status."""

    def __init__(
        self,
        provider_id: str,
        collect_fn: Optional[Callable[[], Dict]] = None,
        freshness_seconds: float = STALE_AFTER_SEC,
        monotonic: Callable[[], float] = time.monotonic,
        epoch: Callable[[], float] = time.time,
    ) -> None:
        self._provider_id = provider_id
        self._collect_fn = collect_fn
        self._freshness_seconds = freshness_seconds
        self._monotonic = monotonic
        self._epoch = epoch
        self._lock = threading.Lock()
        self._plan: Optional[str] = None
        self._windows: Optional[List[Dict]] = None
        self._snapshot_mono: Optional[float] = None
        self._updated_at_epoch: Optional[int] = None
        self._status = STATUS_UNAVAILABLE
        self._error_code = ERROR_NO_SNAPSHOT

    def store(self, plan: Optional[str], windows: List[Dict]) -> None:
        """Publish one successful round; keeps the previous windows intact."""
        with self._lock:
            self._plan = plan if isinstance(plan, str) else None
            self._windows = [dict(window) for window in windows]
            self._snapshot_mono = self._monotonic()
            self._updated_at_epoch = int(self._epoch())
            self._status = STATUS_OK
            self._error_code = ERROR_OK

    def set_failure(self, status: str, error_code: str) -> None:
        """Record a failed round. Last valid values stay cached for ``stale``."""
        with self._lock:
            self._status = status
            self._error_code = error_code

    def run_once(self) -> bool:
        """Run one collection round through ``collect_fn``.

        Any exception is contained here so a broken provider source never
        kills the scheduler thread (goal failure paths).
        """
        if self._collect_fn is None:
            raise RuntimeError("run_once requires a collect_fn")
        try:
            result = self._collect_fn()
        except QuotaError as error:
            self.set_failure(error.status, error.error_code)
            return False
        except Exception:
            self.set_failure(STATUS_UNAVAILABLE, ERROR_UNAVAILABLE)
            return False
        windows = result.get("windows") if isinstance(result, dict) else None
        if not windows:
            self.set_failure(STATUS_INVALID_DATA, ERROR_INVALID_DATA)
            return False
        self.store(result.get("plan"), windows)
        return True

    def current_status(self) -> str:
        """Business status; a successful snapshot older than the window is stale."""
        with self._lock:
            windows = self._windows
            snapshot_mono = self._snapshot_mono
            status = self._status
        if windows is None or snapshot_mono is None:
            return status
        if status not in (STATUS_OK, STATUS_UNAVAILABLE, STATUS_INVALID_DATA):
            return status
        if self._monotonic() - snapshot_mono > self._freshness_seconds:
            return STATUS_STALE
        return status

    def snapshot(self) -> Dict:
        """Build one self-consistent API v1 response (no lock held for JSON)."""
        status = self.current_status()
        now_epoch = int(self._epoch())
        with self._lock:
            plan = self._plan
            windows = self._windows
            updated_at = self._updated_at_epoch
            error_code = self._error_code
        if windows is None:
            plan = None
        if status == STATUS_STALE:
            error_code = ERROR_STALE
        return {
            "schema_version": SCHEMA_VERSION,
            "status": status,
            "data": {
                "provider_id": self._provider_id,
                "plan": plan,
                "windows": (
                    []
                    if windows is None
                    else [
                        window_payload(
                            window["label"],
                            window["remaining_percent"],
                            window["resets_at"],
                            now_epoch,
                        )
                        for window in windows
                    ]
                ),
                "updated_at_epoch": updated_at,
            },
            "error_code": error_code,
        }


class QuotaWorker:
    """Simple periodic poll loop for providers without a notification source."""

    def __init__(
        self, cache: QuotaCache, period_seconds: float = POLL_INTERVAL_SEC
    ) -> None:
        self._cache = cache
        self._period_seconds = period_seconds
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(
            target=self._run, name="quota-poll", daemon=True
        )
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=max(self._period_seconds * 2, 1.0))
            self._thread = None

    def _run(self) -> None:
        while not self._stop_event.is_set():
            started = time.monotonic()
            self._cache.run_once()
            elapsed = time.monotonic() - started
            self._stop_event.wait(max(self._period_seconds - elapsed, 0.0))
