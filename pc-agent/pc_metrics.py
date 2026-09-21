"""Background PC metrics collection with a thread-safe snapshot cache.

The collector runs in its own thread and refreshes one immutable metrics
snapshot about once per second. HTTP worker threads only read copies of
that cache; they never trigger collection themselves (goal node 11,
decision 5), so any number of concurrent devices can poll the Agent
without changing the sampling rate or tearing a response.

Field semantics follow the frozen API v1 contract:

- ``cpu_percent`` and ``memory_percent`` are the core fields. A round
  without both is a failed round: the last good cache stays but the
  response must tell the truth about its age.
- ``gpu_percent``, ``cpu_temperature_c`` and ``gpu_temperature_c`` are
  optional. An unavailable source becomes JSON ``null``; a real 0 is
  never used to mean "unknown" (goal decision 7).
- ``status`` is ``ok`` when every optional field is present, ``degraded``
  when the core fields are valid but any optional field is missing,
  ``stale`` once the cache ages past the freshness window and
  ``unavailable`` when there is no successful snapshot at all.

Third-party dependencies are limited to ``psutil`` (goal node 11,
"Collection and caching"); GPU data comes from the NVIDIA ``nvidia-smi``
command line tool when it is available.
"""

from __future__ import annotations

import math
import subprocess
import threading
import time
from typing import Callable, Dict, Optional, Tuple

SCHEMA_VERSION = 1

# The device treats a snapshot older than three seconds as expired; the
# Agent keeps answering for a while so clients see the truth as ``stale``
# instead of a connection error, but never pretends the data is live.
FRESHNESS_SECONDS = 10.0

STATUS_OK = "ok"
STATUS_DEGRADED = "degraded"
STATUS_STALE = "stale"
STATUS_UNAVAILABLE = "unavailable"

ERROR_OK = None
ERROR_HARDWARE_UNAVAILABLE = "hardware_metrics_unavailable"
ERROR_STALE = "stale_metrics"
ERROR_NO_METRICS = "no_metrics_available"

# Percent fields share one validity window; temperatures get a wider one
# that still rejects the obviously broken values (-999 and friends).
PERCENT_MIN = 0.0
PERCENT_MAX = 100.0
TEMPERATURE_MIN = -100.0
TEMPERATURE_MAX = 150.0

# The NVIDIA query returns utilization and temperature for every GPU it
# can see. The multi-GPU semantics are not settled yet (goal node 11,
# "Collection and caching"), so the values are only published when the
# tool reports exactly one GPU.
_NVIDIA_SMI_COMMAND = (
    "nvidia-smi",
    "--query-gpu=utilization.gpu,temperature.gpu",
    "--format=csv,noheader,nounits",
)
_NVIDIA_SMI_TIMEOUT_S = 2.0

# Preferred Linux temperature sensor names, best first. Windows has no
# psutil temperature API at all, which is an allowed degradation (goal
# node 11, "Collection and caching").
_CPU_TEMPERATURE_SENSOR_PRIORITY = (
    "coretemp",
    "k10temp",
    "cpu_thermal",
    "cpu-thermal",
    "acpitz",
)


def _finite_number(value) -> Optional[float]:
    """Return ``value`` as a float when it is a finite real number."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    number = float(value)
    if not math.isfinite(number):
        return None
    return number


def _clamp_valid(value, minimum: float, maximum: float) -> Optional[float]:
    """Sanitize one metric: finite numbers inside the window pass."""
    number = _finite_number(value)
    if number is None or number < minimum or number > maximum:
        return None
    return number


def _sanitize(raw: Optional[Dict]) -> Optional[Dict[str, Optional[float]]]:
    """Validate one raw sample. Core fields decide success (goal contract)."""
    if not isinstance(raw, dict):
        return None
    clean = {
        "cpu_percent": _clamp_valid(raw.get("cpu_percent"), PERCENT_MIN, PERCENT_MAX),
        "memory_percent": _clamp_valid(raw.get("memory_percent"), PERCENT_MIN, PERCENT_MAX),
        "gpu_percent": _clamp_valid(raw.get("gpu_percent"), PERCENT_MIN, PERCENT_MAX),
        "cpu_temperature_c": _clamp_valid(
            raw.get("cpu_temperature_c"), TEMPERATURE_MIN, TEMPERATURE_MAX
        ),
        "gpu_temperature_c": _clamp_valid(
            raw.get("gpu_temperature_c"), TEMPERATURE_MIN, TEMPERATURE_MAX
        ),
    }
    if clean["cpu_percent"] is None or clean["memory_percent"] is None:
        return None
    return clean


def _status_of(data: Dict[str, Optional[float]]) -> Tuple[str, Optional[str]]:
    """ok only when every optional field survived, degraded otherwise."""
    optional = (
        data["gpu_percent"],
        data["cpu_temperature_c"],
        data["gpu_temperature_c"],
    )
    if all(value is not None for value in optional):
        return STATUS_OK, ERROR_OK
    return STATUS_DEGRADED, ERROR_HARDWARE_UNAVAILABLE


def _nvidia_smi_sample() -> Tuple[Optional[float], Optional[float]]:
    """Query one NVIDIA GPU, or (None, None) on any failure.

    Every failure mode - missing tool, timeout, non-zero exit, unreadable
    output, multiple GPUs - degrades to null instead of killing the
    collection thread (goal node 11, "Collection and caching").
    """
    try:
        completed = subprocess.run(
            _NVIDIA_SMI_COMMAND,
            capture_output=True,
            timeout=_NVIDIA_SMI_TIMEOUT_S,
            check=True,
        )
    except (OSError, subprocess.SubprocessError):
        return None, None
    lines = [
        line.strip()
        for line in completed.stdout.decode("utf-8", "replace").splitlines()
        if line.strip()
    ]
    if len(lines) != 1:
        return None, None
    cells = [cell.strip() for cell in lines[0].split(",")]
    if len(cells) != 2:
        return None, None
    utilization = _clamp_valid(_to_float(cells[0]), PERCENT_MIN, PERCENT_MAX)
    temperature = _clamp_valid(_to_float(cells[1]), TEMPERATURE_MIN, TEMPERATURE_MAX)
    return utilization, temperature


def _to_float(text: str) -> Optional[float]:
    try:
        return float(text)
    except ValueError:
        return None


def _cpu_temperature_psutil(psutil_module) -> Optional[float]:
    """Best-effort CPU temperature through psutil, None when unsupported."""
    sensors = getattr(psutil_module, "sensors_temperatures", None)
    if sensors is None:
        return None
    try:
        entries = sensors()
    except (OSError, AttributeError):
        return None
    if not entries:
        return None
    for name in _CPU_TEMPERATURE_SENSOR_PRIORITY:
        group = entries.get(name)
        if group:
            value = _clamp_valid(group[0].current, TEMPERATURE_MIN, TEMPERATURE_MAX)
            if value is not None:
                return value
    return None


def default_collect_fn() -> Dict[str, Optional[float]]:
    """The real provider set: psutil for CPU and memory, nvidia-smi for GPU."""
    import psutil

    # The first cpu_percent(interval=None) call of a process returns 0.0;
    # priming once here keeps the first published sample meaningful.
    cpu_percent = psutil.cpu_percent(interval=None)
    memory_percent = psutil.virtual_memory().percent
    gpu_percent, gpu_temperature = _nvidia_smi_sample()
    return {
        "cpu_percent": cpu_percent,
        "memory_percent": memory_percent,
        "gpu_percent": gpu_percent,
        "cpu_temperature_c": _cpu_temperature_psutil(psutil),
        "gpu_temperature_c": gpu_temperature,
    }


class PcMetricsCollector:
    """Owns the background sampling and the thread-safe cached snapshot."""

    def __init__(
        self,
        collect_fn: Optional[Callable[[], Dict]] = None,
        freshness_seconds: float = FRESHNESS_SECONDS,
        monotonic: Callable[[], float] = time.monotonic,
        epoch: Callable[[], float] = time.time,
    ) -> None:
        self._collect_fn = collect_fn or default_collect_fn
        self._freshness_seconds = freshness_seconds
        self._monotonic = monotonic
        self._epoch = epoch
        self._lock = threading.Lock()
        self._cache: Optional[Dict] = None
        self._cache_mono: float = 0.0

    def run_once(self) -> bool:
        """Run one collection round and update the cache on core success.

        Any exception inside a provider is caught here so the background
        thread survives broken hardware sources (goal node 11, "Collection
        and caching"). Returns True when a fresh core snapshot was stored.
        """
        try:
            sanitized = _sanitize(self._collect_fn())
        except Exception:
            return False
        if sanitized is None:
            return False
        status, error_code = _status_of(sanitized)
        with self._lock:
            self._cache = {
                "status": status,
                "error_code": error_code,
                "data": sanitized,
                "updated_at_epoch": int(self._epoch()),
            }
            self._cache_mono = self._monotonic()
        return True

    def snapshot(self) -> Dict:
        """Build one self-consistent API v1 response dict.

        Callers get a private copy; the JSON encoding happens outside the
        lock (goal node 11, "Collection and caching").
        """
        with self._lock:
            cache = self._cache
            cache_mono = self._cache_mono
            now_mono = self._monotonic()
        if cache is None:
            return self._empty_snapshot()
        age = now_mono - cache_mono
        if age > self._freshness_seconds:
            return {
                "schema_version": SCHEMA_VERSION,
                "status": STATUS_STALE,
                "updated_at_epoch": cache["updated_at_epoch"],
                "age_sec": round(age, 2),
                "data": dict(cache["data"]),
                "error_code": ERROR_STALE,
            }
        return {
            "schema_version": SCHEMA_VERSION,
            "status": cache["status"],
            "updated_at_epoch": cache["updated_at_epoch"],
            "age_sec": round(age, 2),
            "data": dict(cache["data"]),
            "error_code": cache["error_code"],
        }

    @staticmethod
    def _empty_snapshot() -> Dict:
        return {
            "schema_version": SCHEMA_VERSION,
            "status": STATUS_UNAVAILABLE,
            "updated_at_epoch": None,
            "age_sec": None,
            "data": {
                "cpu_percent": None,
                "memory_percent": None,
                "gpu_percent": None,
                "cpu_temperature_c": None,
                "gpu_temperature_c": None,
            },
            "error_code": ERROR_NO_METRICS,
        }


class CollectorWorker:
    """Sample loop backing the collector; failures never stop it."""

    def __init__(self, collector: PcMetricsCollector, period_seconds: float = 1.0) -> None:
        self._collector = collector
        self._period_seconds = period_seconds
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(target=self._run, name="pc-metrics", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=self._period_seconds * 2)
            self._thread = None

    def _run(self) -> None:
        while not self._stop_event.is_set():
            started = time.monotonic()
            self._collector.run_once()
            elapsed = time.monotonic() - started
            self._stop_event.wait(max(self._period_seconds - elapsed, 0.0))