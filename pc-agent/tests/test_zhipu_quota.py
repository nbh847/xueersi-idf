"""Tests for the Zhipu quota adapter: discovery, normalization and fetch."""

import json
import os
import sys
import tempfile
import unittest
import urllib.error
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from quota_cache import (  # noqa: E402
    ERROR_AUTH_REQUIRED,
    ERROR_INVALID_DATA,
    ERROR_UNAVAILABLE,
    STATUS_AUTH_REQUIRED,
    STATUS_INVALID_DATA,
    STATUS_UNAVAILABLE,
    QuotaError,
)
from zhipu_quota import (  # noqa: E402
    MAX_RESPONSE_BYTES,
    ZhipuCollector,
    build_quota_url,
    discover_credentials,
    fetch_quota,
    normalize_quota_limit,
    supported_host,
)

FAKE_TOKEN = "test-token-not-real"
MS_RESET = 1_800_000_000_000


def _limit(limit_type="TOKENS_LIMIT", unit=3, percentage=36, next_reset=MS_RESET):
    return {
        "type": limit_type,
        "unit": unit,
        "percentage": percentage,
        "nextResetTime": next_reset,
        "number": 1,
    }


class FakeResponse:
    def __init__(self, body):
        self._body = body

    def read(self, size=-1):
        if size is None or size < 0:
            return self._body
        return self._body[:size]

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class SupportedHostTest(unittest.TestCase):
    def test_verified_hosts_pass(self) -> None:
        self.assertEqual(supported_host("https://open.bigmodel.cn/api/anthropic"), "open.bigmodel.cn")
        self.assertEqual(supported_host("https://dev.bigmodel.cn/api/anthropic"), "dev.bigmodel.cn")

    def test_unverified_or_malformed_hosts_fail(self) -> None:
        for base_url in (
            "https://api.z.ai/api/anthropic",
            "http://open.bigmodel.cn/api/anthropic",
            "https://example.com",
            "not a url",
            "",
        ):
            with self.subTest(base_url=base_url):
                self.assertIsNone(supported_host(base_url))

    def test_quota_url_uses_verified_origin(self) -> None:
        self.assertEqual(
            build_quota_url("https://open.bigmodel.cn/api/anthropic"),
            "https://open.bigmodel.cn/api/monitor/usage/quota/limit",
        )


class CredentialDiscoveryTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.home = self._tmp.name

    def tearDown(self) -> None:
        self._tmp.cleanup()

    def _write(self, relative, payload) -> None:
        path = os.path.join(self.home, relative)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(payload if isinstance(payload, str) else json.dumps(payload))

    def test_models_json_wins_over_other_sources(self) -> None:
        self._write(
            os.path.join(".qclaw", "agents", "main", "agent", "models.json"),
            {"providers": {"zhipu": {"baseUrl": "https://open.bigmodel.cn/api/anthropic", "apiKey": FAKE_TOKEN}}},
        )
        self._write(
            os.path.join(".claude", "settings.json"),
            {"env": {"ANTHROPIC_BASE_URL": "https://dev.bigmodel.cn/api/anthropic", "ANTHROPIC_AUTH_TOKEN": "other"}},
        )
        found = discover_credentials(self.home, {"ANTHROPIC_AUTH_TOKEN": "env-token"})
        self.assertEqual(found.source, "models.json")
        self.assertEqual(found.token, FAKE_TOKEN)

    def test_models_json_skips_unverified_provider(self) -> None:
        self._write(
            os.path.join(".qclaw", "agents", "main", "agent", "models.json"),
            {"providers": {"zai": {"baseUrl": "https://api.z.ai/api/anthropic", "apiKey": FAKE_TOKEN}}},
        )
        self._write(
            os.path.join(".claude", "settings.json"),
            {"env": {"ANTHROPIC_BASE_URL": "https://dev.bigmodel.cn/api/anthropic", "ANTHROPIC_AUTH_TOKEN": "claude-token"}},
        )
        found = discover_credentials(self.home, {})
        self.assertEqual(found.source, "claude-settings")
        self.assertEqual(found.token, "claude-token")

    def test_environment_is_the_last_fallback(self) -> None:
        found = discover_credentials(
            self.home,
            {
                "ANTHROPIC_BASE_URL": "https://open.bigmodel.cn/api/anthropic",
                "ANTHROPIC_AUTH_TOKEN": "env-token",
            },
        )
        self.assertEqual(found.source, "environment")

    def test_broken_or_incomplete_config_is_ignored(self) -> None:
        self._write(os.path.join(".qclaw", "agents", "main", "agent", "models.json"), "{not json")
        self._write(os.path.join(".claude", "settings.json"), {"env": {"ANTHROPIC_BASE_URL": "https://open.bigmodel.cn"}})
        self.assertIsNone(discover_credentials(self.home, {}))

    def test_unverified_environment_domain_is_rejected(self) -> None:
        found = discover_credentials(
            self.home,
            {
                "ANTHROPIC_BASE_URL": "https://api.z.ai/api/anthropic",
                "ANTHROPIC_AUTH_TOKEN": "env-token",
            },
        )
        self.assertIsNone(found)

    def test_missing_everything_returns_none(self) -> None:
        self.assertIsNone(discover_credentials(self.home, {}))


class NormalizeQuotaLimitTest(unittest.TestCase):
    def test_unit_mapping_and_millisecond_conversion(self) -> None:
        raw = {"data": {"level": "pro", "limits": [_limit(unit=3, percentage=36), _limit(unit=6, percentage=72)]}}
        normalized = normalize_quota_limit(raw)
        self.assertEqual([window["label"] for window in normalized["windows"]], ["5H", "1W"])
        self.assertEqual(normalized["windows"][0]["remaining_percent"], 64)
        self.assertEqual(normalized["windows"][1]["remaining_percent"], 28)
        self.assertEqual(normalized["windows"][0]["resets_at"], 1_800_000_000)
        self.assertEqual(normalized["plan"], "pro")

    def test_order_independent_with_mcp_entry_between(self) -> None:
        raw = {
            "data": {
                "limits": [
                    _limit(limit_type="TIME_LIMIT", unit=5),
                    _limit(unit=6, percentage=10),
                    _limit(unit=3, percentage=20),
                ]
            }
        }
        normalized = normalize_quota_limit(raw)
        self.assertEqual([window["label"] for window in normalized["windows"]], ["5H", "1W"])

    def test_string_unit_is_accepted(self) -> None:
        raw = {"data": {"limits": [_limit(unit="6", percentage=10)]}}
        self.assertEqual(normalize_quota_limit(raw)["windows"][0]["label"], "1W")

    def test_unknown_unit_or_type_is_dropped(self) -> None:
        raw = {
            "data": {
                "limits": [
                    _limit(unit=7),
                    _limit(unit=5),
                    _limit(limit_type="TIME_LIMIT", unit=3),
                ]
            }
        }
        with self.assertRaises(QuotaError) as raised:
            normalize_quota_limit(raw)
        self.assertEqual(raised.exception.error_code, ERROR_INVALID_DATA)

    def test_partial_window_keeps_the_valid_one(self) -> None:
        raw = {"data": {"limits": [_limit(unit=3, percentage=100), _limit(unit=99)]}}
        normalized = normalize_quota_limit(raw)
        self.assertEqual([window["label"] for window in normalized["windows"]], ["5H"])
        self.assertEqual(normalized["windows"][0]["remaining_percent"], 0)

    def test_invalid_percentage_is_dropped(self) -> None:
        for percentage in (None, True, "36", float("nan"), float("inf")):
            with self.subTest(percentage=percentage):
                raw = {"data": {"limits": [_limit(unit=3, percentage=percentage)]}}
                with self.assertRaises(QuotaError):
                    normalize_quota_limit(raw)

    def test_invalid_reset_time_keeps_window_with_unknown_time(self) -> None:
        for next_reset in (0, -1, None, "1800000000000", 1):
            with self.subTest(next_reset=next_reset):
                raw = {"data": {"limits": [_limit(unit=3, next_reset=next_reset)]}}
                window = normalize_quota_limit(raw)["windows"][0]
                self.assertIsNone(window["resets_at"])

    def test_malformed_payload_is_invalid_data(self) -> None:
        for raw in (None, {}, {"data": None}, {"data": {}}, {"data": {"limits": {}}}):
            with self.subTest(raw=raw):
                with self.assertRaises(QuotaError) as raised:
                    normalize_quota_limit(raw)
                self.assertEqual(raised.exception.status, STATUS_INVALID_DATA)

    def test_non_string_level_becomes_none(self) -> None:
        raw = {"data": {"level": 7, "limits": [_limit()]}}
        self.assertIsNone(normalize_quota_limit(raw)["plan"])


class FetchQuotaTest(unittest.TestCase):
    URL = "https://open.bigmodel.cn/api/monitor/usage/quota/limit"

    def _urlopen(self, *, body=b"{}", error=None, captured=None):
        def fake_urlopen(request, timeout=None):
            if captured is not None:
                captured.append(request)
            if error is not None:
                raise error
            return FakeResponse(body)

        return mock.patch("urllib.request.urlopen", fake_urlopen)

    def test_successful_read_sends_raw_token_header(self) -> None:
        captured = []
        body = json.dumps({"data": {"limits": []}}).encode()
        with self._urlopen(body=body, captured=captured):
            parsed = fetch_quota(self.URL, FAKE_TOKEN)
        self.assertEqual(parsed, {"data": {"limits": []}})
        request = captured[0]
        self.assertEqual(request.get_header("Authorization"), FAKE_TOKEN)
        self.assertNotIn("Bearer", request.get_header("Authorization"))

    def test_auth_errors_map_to_auth_required(self) -> None:
        for code in (401, 403):
            with self.subTest(code=code):
                error = urllib.error.HTTPError(self.URL, code, "denied", None, None)
                with self._urlopen(error=error):
                    with self.assertRaises(QuotaError) as raised:
                        fetch_quota(self.URL, FAKE_TOKEN)
                self.assertEqual(raised.exception.status, STATUS_AUTH_REQUIRED)
                self.assertEqual(raised.exception.error_code, ERROR_AUTH_REQUIRED)

    def test_rate_limit_and_server_errors_are_unavailable(self) -> None:
        for code in (429, 500, 502, 503):
            with self.subTest(code=code):
                error = urllib.error.HTTPError(self.URL, code, "busy", None, None)
                with self._urlopen(error=error):
                    with self.assertRaises(QuotaError) as raised:
                        fetch_quota(self.URL, FAKE_TOKEN)
                self.assertEqual(raised.exception.status, STATUS_UNAVAILABLE)
                self.assertEqual(raised.exception.error_code, ERROR_UNAVAILABLE)

    def test_network_error_is_unavailable(self) -> None:
        with self._urlopen(error=urllib.error.URLError("dns")):
            with self.assertRaises(QuotaError) as raised:
                fetch_quota(self.URL, FAKE_TOKEN)
        self.assertEqual(raised.exception.status, STATUS_UNAVAILABLE)

    def test_invalid_json_is_invalid_data(self) -> None:
        with self._urlopen(body=b"<html>nope</html>"):
            with self.assertRaises(QuotaError) as raised:
                fetch_quota(self.URL, FAKE_TOKEN)
        self.assertEqual(raised.exception.status, STATUS_INVALID_DATA)
        self.assertEqual(raised.exception.error_code, ERROR_INVALID_DATA)

    def test_oversized_body_is_invalid_data(self) -> None:
        with self._urlopen(body=b"x" * (MAX_RESPONSE_BYTES + 10)):
            with self.assertRaises(QuotaError) as raised:
                fetch_quota(self.URL, FAKE_TOKEN)
        self.assertEqual(raised.exception.status, STATUS_INVALID_DATA)


class ZhipuCollectorTest(unittest.TestCase):
    def test_missing_configuration_is_auth_required(self) -> None:
        collector = ZhipuCollector(config_loader=lambda: None, fetch_fn=lambda url, token: {})
        with self.assertRaises(QuotaError) as raised:
            collector.collect()
        self.assertEqual(raised.exception.status, STATUS_AUTH_REQUIRED)

    def test_collect_builds_url_and_normalizes(self) -> None:
        from zhipu_quota import ZhipuCredentials

        calls = []

        def fetch_fn(url, token):
            calls.append((url, token))
            return {"data": {"limits": [_limit(unit=3, percentage=36)]}}

        collector = ZhipuCollector(
            config_loader=lambda: ZhipuCredentials(
                "https://open.bigmodel.cn/api/anthropic", FAKE_TOKEN, "environment"
            ),
            fetch_fn=fetch_fn,
        )
        normalized = collector.collect()
        self.assertEqual(calls, [("https://open.bigmodel.cn/api/monitor/usage/quota/limit", FAKE_TOKEN)])
        self.assertEqual(normalized["windows"][0]["remaining_percent"], 64)
        self.assertNotIn(FAKE_TOKEN, json.dumps(normalized))

    def test_fetch_failure_propagates_as_quota_error(self) -> None:
        from zhipu_quota import ZhipuCredentials

        def fetch_fn(url, token):
            raise QuotaError(STATUS_UNAVAILABLE, ERROR_UNAVAILABLE)

        collector = ZhipuCollector(
            config_loader=lambda: ZhipuCredentials(
                "https://open.bigmodel.cn/api/anthropic", FAKE_TOKEN, "environment"
            ),
            fetch_fn=fetch_fn,
        )
        with self.assertRaises(QuotaError):
            collector.collect()


if __name__ == "__main__":
    unittest.main()
