"""Tests for the Codex quota adapter: client, normalization and worker."""

import json
import os
import queue
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from codex_quota import (  # noqa: E402
    AppServerClient,
    AppServerError,
    AppServerUnavailable,
    CodexWorker,
    normalize_rate_limits,
    normalize_window,
)
from quota_cache import (  # noqa: E402
    ERROR_INVALID_DATA,
    STATUS_AUTH_REQUIRED,
    STATUS_INVALID_DATA,
    STATUS_OK,
    STATUS_UNAVAILABLE,
    QuotaCache,
    QuotaError,
)

RESETS_AT = 1_800_000_000


def _rate_limits(
    primary_used=20.0,
    secondary_used=88.0,
    primary_mins=300,
    secondary_mins=10080,
):
    return {
        "rateLimitsByLimitId": {
            "codex": {
                "planType": "plus",
                "primary": {
                    "usedPercent": primary_used,
                    "windowDurationMins": primary_mins,
                    "resetsAt": RESETS_AT,
                },
                "secondary": {
                    "usedPercent": secondary_used,
                    "windowDurationMins": secondary_mins,
                    "resetsAt": RESETS_AT,
                },
            }
        }
    }


class _FakeStdin:
    def __init__(self, on_write=None):
        self.lines = []
        self.closed = False
        self._on_write = on_write

    def write(self, text):
        self.lines.append(text)
        if self._on_write is not None:
            self._on_write(text)

    def flush(self):
        pass

    def close(self):
        self.closed = True


class _ClientExit:
    """Sentinel response: end the stdout stream instead of answering."""


CLIENT_EXIT = _ClientExit()


class FakeAppServerProcess:
    """Request-driven App Server stand-in.

    Every JSON-RPC request is answered from the scripted ``responses`` map,
    so the reader thread can never consume a reply before the request that
    owns it was written. ``prelude`` lines are emitted first, and the
    ``CLIENT_EXIT`` sentinel closes stdout without answering - which is how
    a crashed child looks to the client.
    """

    def __init__(self, responses, prelude=()):
        self._responses = responses
        self._out = queue.Queue()
        for line in prelude:
            self._out.put(line)
        self.stdin = _FakeStdin(self._handle_line)
        self.stdout = self._stdout_iter()
        self.stderr = iter(())
        self._returncode = None

    def _handle_line(self, text):
        try:
            message = json.loads(text)
        except ValueError:
            return
        request_id = message.get("id")
        method = message.get("method")
        if request_id is None or method is None:
            return
        response = self._responses.get(method)
        if response is CLIENT_EXIT:
            self._out.put(None)
            return
        if isinstance(response, Exception):
            line = {"jsonrpc": "2.0", "id": request_id, "error": {"message": str(response)}}
        elif response is None:
            return
        else:
            line = {"jsonrpc": "2.0", "id": request_id, "result": response}
        self._out.put(json.dumps(line) + "\n")

    def _stdout_iter(self):
        while True:
            line = self._out.get()
            if line is None:
                return
            yield line

    def poll(self):
        return self._returncode

    def terminate(self):
        self._returncode = 0
        self._out.put(None)

    def kill(self):
        self._returncode = -9
        self._out.put(None)

    def wait(self, timeout=None):
        if self._returncode is None:
            self._returncode = 0
        return self._returncode


class FakeClient:
    """Scripted App server client used by the worker tests."""

    def __init__(self, responses):
        self._responses = responses
        self._handlers = []
        self.calls = []
        self.started = False
        self.stopped = False

    def add_notification_handler(self, handler):
        self._handlers.append(handler)

    def start(self):
        self.started = True

    def stop(self):
        self.stopped = True

    @property
    def running(self):
        return self.started and not self.stopped

    def request(self, method, params=None):
        self.calls.append(method)
        response = self._responses.get(method)
        if isinstance(response, Exception):
            raise response
        if response is None:
            raise AppServerUnavailable("no scripted response")
        return response


class NormalizeWindowTest(unittest.TestCase):
    def test_fixed_duration_labels(self) -> None:
        self.assertEqual(normalize_window({"usedPercent": 0, "windowDurationMins": 300})["label"], "5H")
        self.assertEqual(
            normalize_window({"usedPercent": 0, "windowDurationMins": 10080})["label"], "7D"
        )

    def test_unknown_or_invalid_duration_is_dropped(self) -> None:
        for duration in (60, 1440, 0, -300, 300.0, "300", True, None):
            with self.subTest(duration=duration):
                self.assertIsNone(
                    normalize_window({"usedPercent": 10, "windowDurationMins": duration})
                )

    def test_invalid_used_percent_is_dropped(self) -> None:
        for used in (None, True, "10", float("nan"), float("inf")):
            with self.subTest(used=used):
                self.assertIsNone(
                    normalize_window({"usedPercent": used, "windowDurationMins": 300})
                )

    def test_reset_timestamp_missing_becomes_none(self) -> None:
        window = normalize_window({"usedPercent": 10, "windowDurationMins": 300})
        self.assertIsNone(window["resets_at"])
        self.assertEqual(window["remaining_percent"], 90)


class NormalizeRateLimitsTest(unittest.TestCase):
    def test_two_target_windows_sorted_deterministically(self) -> None:
        raw = _rate_limits(primary_mins=10080, secondary_mins=300, primary_used=88, secondary_used=20)
        normalized = normalize_rate_limits(raw)
        self.assertEqual([window["label"] for window in normalized["windows"]], ["5H", "7D"])
        self.assertEqual(normalized["windows"][0]["remaining_percent"], 80)
        self.assertEqual(normalized["windows"][1]["remaining_percent"], 12)
        self.assertEqual(normalized["plan"], "plus")

    def test_zero_percent_stays_zero(self) -> None:
        normalized = normalize_rate_limits(_rate_limits(primary_used=100))
        self.assertEqual(normalized["windows"][0]["remaining_percent"], 0)

    def test_partial_window_keeps_the_valid_one(self) -> None:
        normalized = normalize_rate_limits(_rate_limits(primary_mins=60))
        self.assertEqual([window["label"] for window in normalized["windows"]], ["7D"])

    def test_missing_bucket_is_invalid_data(self) -> None:
        for raw in ({}, {"rateLimitsByLimitId": {}}, {"rateLimitsByLimitId": {"codex": None}}, None):
            with self.subTest(raw=raw):
                with self.assertRaises(QuotaError) as raised:
                    normalize_rate_limits(raw)
                self.assertEqual(raised.exception.error_code, ERROR_INVALID_DATA)

    def test_both_windows_invalid_is_invalid_data(self) -> None:
        raw = _rate_limits(primary_used=None, secondary_mins=60)
        with self.assertRaises(QuotaError):
            normalize_rate_limits(raw)

    def test_non_string_plan_becomes_none(self) -> None:
        raw = _rate_limits()
        raw["rateLimitsByLimitId"]["codex"]["planType"] = 7
        self.assertIsNone(normalize_rate_limits(raw)["plan"])


class AppServerClientTest(unittest.TestCase):
    def _started_client(self, responses, prelude=()):
        proc = FakeAppServerProcess(responses, prelude=prelude)
        client = AppServerClient(
            spawner=lambda: proc, request_timeout=2.0, handshake_timeout=2.0
        )
        client.start()
        return client, proc

    def test_handshake_then_request(self) -> None:
        client, proc = self._started_client(
            {
                "initialize": {},
                "account/read": {"account": {"type": "chatgpt"}},
            }
        )
        try:
            self.assertTrue(client.running)
            self.assertEqual(
                client.request("account/read", {"refreshToken": False}),
                {"account": {"type": "chatgpt"}},
            )
            self.assertTrue(any("initialize" in line for line in proc.stdin.lines))
        finally:
            client.stop()
        self.assertFalse(client.running)

    def test_process_exit_fails_pending_request(self) -> None:
        client, _ = self._started_client(
            {"initialize": {}, "account/rateLimits/read": CLIENT_EXIT}
        )
        try:
            with self.assertRaises(AppServerUnavailable):
                client.request("account/rateLimits/read")
        finally:
            client.stop()

    def test_jsonrpc_error_is_raised(self) -> None:
        client, _ = self._started_client(
            {"initialize": {}, "account/read": AppServerError("boom")}
        )
        try:
            with self.assertRaises(AppServerError):
                client.request("account/read")
        finally:
            client.stop()

    def test_invalid_json_lines_are_skipped(self) -> None:
        client, _ = self._started_client({"initialize": {}}, prelude=["this is not json\n"])
        try:
            self.assertTrue(client.running)
        finally:
            client.stop()

    def test_request_before_start_is_unavailable(self) -> None:
        with self.assertRaises(AppServerUnavailable):
            AppServerClient(spawner=lambda: FakeAppServerProcess({})).request("account/read")


class CodexWorkerTest(unittest.TestCase):
    def _worker(self, responses):
        cache = QuotaCache("codex")
        client = FakeClient(responses)
        worker = CodexWorker(cache, client_factory=lambda: client, poll_interval=0.05)
        return cache, client, worker

    def test_chatgpt_login_stores_two_windows(self) -> None:
        cache, client, worker = self._worker(
            {
                "account/read": {"account": {"type": "chatgpt"}},
                "account/rateLimits/read": _rate_limits(),
            }
        )
        self.assertTrue(worker._update_once(client))
        snapshot = cache.snapshot()
        self.assertEqual(snapshot["status"], STATUS_OK)
        self.assertEqual(snapshot["data"]["plan"], "plus")
        self.assertEqual(
            [window["label"] for window in snapshot["data"]["windows"]], ["5H", "7D"]
        )

    def test_non_chatgpt_login_is_auth_required_without_data(self) -> None:
        cache, client, worker = self._worker(
            {"account/read": {"account": {"type": "apiKey"}}}
        )
        self.assertFalse(worker._update_once(client))
        snapshot = cache.snapshot()
        self.assertEqual(snapshot["status"], STATUS_AUTH_REQUIRED)
        self.assertEqual(snapshot["data"]["windows"], [])
        # A later read must not be attempted while the login is unconfirmed.
        self.assertNotIn("account/rateLimits/read", client.calls)

    def test_account_probe_failure_is_unavailable_and_propagates(self) -> None:
        cache, client, worker = self._worker(
            {"account/read": AppServerUnavailable("process gone")}
        )
        with self.assertRaises(AppServerUnavailable):
            worker._update_once(client)
        self.assertEqual(cache.snapshot()["status"], STATUS_UNAVAILABLE)

    def test_quota_request_error_is_unavailable(self) -> None:
        cache, client, worker = self._worker(
            {
                "account/read": {"account": {"type": "chatgpt"}},
                "account/rateLimits/read": AppServerError("bad request"),
            }
        )
        self.assertFalse(worker._update_once(client))
        self.assertEqual(cache.snapshot()["status"], STATUS_UNAVAILABLE)

    def test_invalid_data_keeps_last_values(self) -> None:
        cache, client, worker = self._worker(
            {
                "account/read": {"account": {"type": "chatgpt"}},
                "account/rateLimits/read": _rate_limits(),
            }
        )
        self.assertTrue(worker._update_once(client))
        client._responses["account/rateLimits/read"] = {"rateLimitsByLimitId": {}}
        self.assertFalse(worker._update_once(client))
        snapshot = cache.snapshot()
        self.assertEqual(snapshot["status"], STATUS_INVALID_DATA)
        self.assertTrue(snapshot["data"]["windows"])

    def test_notification_wakes_and_resets_auth(self) -> None:
        cache, client, worker = self._worker({})
        worker._handle_notification("account/updated", None)
        self.assertFalse(worker._auth_confirmed)

    def test_worker_loop_reaches_ok_and_stops(self) -> None:
        cache, client, worker = self._worker(
            {
                "account/read": {"account": {"type": "chatgpt"}},
                "account/rateLimits/read": _rate_limits(),
            }
        )
        worker.start()
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline and cache.snapshot()["status"] != STATUS_OK:
            time.sleep(0.02)
        worker.stop()
        self.assertEqual(cache.snapshot()["status"], STATUS_OK)
        self.assertTrue(client.stopped)


if __name__ == "__main__":
    unittest.main()
