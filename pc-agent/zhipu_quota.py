"""Zhipu (智谱) quota adapter: read-only credential discovery and query.

Credential discovery mirrors the frozen reference order (goal decision 5):

1. ``~/.qclaw/agents/main/agent/models.json`` providers whose ``baseUrl``
   points at a supported Zhipu host.
2. ``~/.claude/settings.json`` ``env.ANTHROPIC_BASE_URL`` /
   ``ANTHROPIC_AUTH_TOKEN``.
3. Process environment ``ANTHROPIC_BASE_URL`` / ``ANTHROPIC_AUTH_TOKEN``.

Only the two verified hosts ``open.bigmodel.cn`` and ``dev.bigmodel.cn``
are accepted; an unverified domain (including ``api.z.ai``) is treated as
"no usable configuration" instead of being guessed. The token is only sent
as the ``Authorization`` header, and no credential, upstream body or
account identifier is logged, stored or forwarded (goal decision 6).
"""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.parse
import urllib.request
from typing import Callable, Dict, List, Optional, Tuple

from quota_cache import (
    ERROR_AUTH_REQUIRED,
    ERROR_INVALID_DATA,
    ERROR_UNAVAILABLE,
    STATUS_AUTH_REQUIRED,
    STATUS_INVALID_DATA,
    STATUS_UNAVAILABLE,
    QuotaError,
    finite_number,
    remaining_percent,
)

PROVIDER_ID = "zhipu"

SUPPORTED_HOSTS = ("open.bigmodel.cn", "dev.bigmodel.cn")
QUOTA_PATH = "/api/monitor/usage/quota/limit"
REQUEST_TIMEOUT_SEC = 10.0
MAX_RESPONSE_BYTES = 256 * 1024
USER_AGENT = "xiaomiao-agent/0.1.0"

# Frozen unit mapping (2026-08-20, re-confirmed 2026-09-27):
# TOKENS_LIMIT unit 3 is the 5 hour window and unit 6 the weekly one.
# TIME_LIMIT unit 5 is the MCP monthly quota and stays out of scope.
TOKEN_UNIT_LABELS = {3: "5H", 6: "1W"}
WINDOW_ORDER = ("5H", "1W")

MODELS_JSON_PATH = (".qclaw", "agents", "main", "agent", "models.json")
CLAUDE_SETTINGS_PATH = (".claude", "settings.json")


class ZhipuCredentials:
    """One usable base URL plus token; the source is kept for diagnostics only."""

    def __init__(self, base_url: str, token: str, source: str) -> None:
        self.base_url = base_url
        self.token = token
        self.source = source


def supported_host(base_url: str) -> Optional[str]:
    """Return the verified host of ``base_url``, or None when unsupported."""
    try:
        parsed = urllib.parse.urlparse(base_url)
    except ValueError:
        return None
    if parsed.scheme != "https" or not parsed.hostname:
        return None
    host = parsed.hostname.lower()
    return host if host in SUPPORTED_HOSTS else None


def build_quota_url(base_url: str) -> str:
    """Quota endpoint on the verified origin of the configured base URL."""
    parsed = urllib.parse.urlparse(base_url)
    return "%s://%s%s" % (parsed.scheme, parsed.hostname, QUOTA_PATH)


def _load_json(path: str):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError):
        return None


def _from_models_json(home: str) -> Optional[ZhipuCredentials]:
    raw = _load_json(os.path.join(home, *MODELS_JSON_PATH))
    providers = raw.get("providers") if isinstance(raw, dict) else None
    if not isinstance(providers, dict):
        return None
    for provider in providers.values():
        if not isinstance(provider, dict):
            continue
        base_url = provider.get("baseUrl")
        api_key = provider.get("apiKey")
        if not isinstance(base_url, str) or not isinstance(api_key, str) or not api_key:
            continue
        if supported_host(base_url) is None:
            continue
        return ZhipuCredentials(base_url, api_key, "models.json")
    return None


def _from_claude_settings(home: str) -> Optional[ZhipuCredentials]:
    raw = _load_json(os.path.join(home, *CLAUDE_SETTINGS_PATH))
    env = raw.get("env") if isinstance(raw, dict) else None
    if not isinstance(env, dict):
        return None
    base_url = env.get("ANTHROPIC_BASE_URL")
    token = env.get("ANTHROPIC_AUTH_TOKEN")
    if not isinstance(base_url, str) or not isinstance(token, str) or not token:
        return None
    if supported_host(base_url) is None:
        return None
    return ZhipuCredentials(base_url, token, "claude-settings")


def _from_environment(environ) -> Optional[ZhipuCredentials]:
    base_url = environ.get("ANTHROPIC_BASE_URL")
    token = environ.get("ANTHROPIC_AUTH_TOKEN")
    if not isinstance(base_url, str) or not isinstance(token, str) or not token:
        return None
    if supported_host(base_url) is None:
        return None
    return ZhipuCredentials(base_url, token, "environment")


def discover_credentials(
    home: Optional[str] = None, environ=None
) -> Optional[ZhipuCredentials]:
    """Look for a Zhipu token in the frozen source order; None when absent."""
    home = home if home is not None else os.path.expanduser("~")
    environ = environ if environ is not None else os.environ
    for loader in (_from_models_json, _from_claude_settings):
        found = loader(home)
        if found is not None:
            return found
    return _from_environment(environ)


def _unit_as_int(value) -> Optional[int]:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str) and value.strip().isdigit():
        return int(value.strip())
    return None


def _millis_to_seconds(value) -> Optional[int]:
    """Unix milliseconds to Unix seconds; invalid or non-positive is unknown."""
    number = finite_number(value)
    if number is None or number <= 0:
        return None
    seconds = int(number // 1000)
    return seconds if seconds > 0 else None


def normalize_quota_limit(raw) -> Dict:
    """Normalize ``/api/monitor/usage/quota/limit`` into the two target windows.

    Only ``TOKENS_LIMIT`` entries with unit 3 or 6 are read. Unknown type or
    unit and invalid numbers are dropped rather than inferred from order or
    from the reset timestamp; when neither target window survives the round
    is ``invalid_data`` (goal decision 5).
    """
    data = raw.get("data") if isinstance(raw, dict) else None
    if not isinstance(data, dict):
        raise QuotaError(STATUS_INVALID_DATA, ERROR_INVALID_DATA)
    limits = data.get("limits")
    if not isinstance(limits, list):
        raise QuotaError(STATUS_INVALID_DATA, ERROR_INVALID_DATA)

    plan = data.get("level")
    if not isinstance(plan, str) or not plan:
        plan = None

    found: Dict[str, Dict] = {}
    for item in limits:
        if not isinstance(item, dict) or item.get("type") != "TOKENS_LIMIT":
            continue
        label = TOKEN_UNIT_LABELS.get(_unit_as_int(item.get("unit")))
        if label is None:
            continue
        remaining = remaining_percent(item.get("percentage"))
        if remaining is None:
            continue
        found[label] = {
            "label": label,
            "remaining_percent": remaining,
            "resets_at": _millis_to_seconds(item.get("nextResetTime")),
        }

    windows: List[Dict] = [found[label] for label in WINDOW_ORDER if label in found]
    if not windows:
        raise QuotaError(STATUS_INVALID_DATA, ERROR_INVALID_DATA)
    return {"plan": plan, "windows": windows}


def fetch_quota(url: str, token: str, timeout: float = REQUEST_TIMEOUT_SEC):
    """One bounded read-only HTTPS GET; returns the parsed JSON body.

    HTTP 401/403 means the token is not usable; network, timeout, 429 and
    5xx are ``unavailable``; an unreadable or oversized body is
    ``invalid_data``. The response is never echoed anywhere.
    """
    request = urllib.request.Request(
        url,
        headers={"Authorization": token, "Accept": "application/json", "User-Agent": USER_AGENT},
        method="GET",
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read(MAX_RESPONSE_BYTES + 1)
    except urllib.error.HTTPError as error:
        if error.code in (401, 403):
            raise QuotaError(STATUS_AUTH_REQUIRED, ERROR_AUTH_REQUIRED) from None
        raise QuotaError(STATUS_UNAVAILABLE, ERROR_UNAVAILABLE) from None
    except (urllib.error.URLError, OSError, ValueError):
        raise QuotaError(STATUS_UNAVAILABLE, ERROR_UNAVAILABLE) from None
    if len(body) > MAX_RESPONSE_BYTES:
        raise QuotaError(STATUS_INVALID_DATA, ERROR_INVALID_DATA)
    try:
        return json.loads(body.decode("utf-8", "replace"))
    except ValueError:
        raise QuotaError(STATUS_INVALID_DATA, ERROR_INVALID_DATA) from None


class ZhipuCollector:
    """Credential discovery plus one query round for :class:`QuotaCache`."""

    def __init__(
        self,
        config_loader: Callable[[], Optional[ZhipuCredentials]] = discover_credentials,
        fetch_fn: Optional[Callable[[str, str], object]] = None,
    ) -> None:
        self._config_loader = config_loader
        self._fetch_fn = fetch_fn or fetch_quota

    def collect(self) -> Dict:
        credentials = self._config_loader()
        if credentials is None:
            raise QuotaError(STATUS_AUTH_REQUIRED, ERROR_AUTH_REQUIRED)
        raw = self._fetch_fn(build_quota_url(credentials.base_url), credentials.token)
        return normalize_quota_limit(raw)
