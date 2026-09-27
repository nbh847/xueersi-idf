"""HTTP contract tests for the Xiaomiao Agent API v1 routes."""

import json
import os
import sys
import threading
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from monitor import build_health_payload, build_server  # noqa: E402
from pc_metrics import PcMetricsCollector  # noqa: E402
from quota_cache import (  # noqa: E402
    ERROR_AUTH_REQUIRED,
    ERROR_INVALID_DATA,
    STATUS_AUTH_REQUIRED,
    STATUS_INVALID_DATA,
    STATUS_OK,
    QuotaCache,
    QuotaError,
)


def _sample(cpu=23.4, memory=61.2, gpu=72.0, cpu_temp=55.0, gpu_temp=64.0):
    return {
        "cpu_percent": cpu,
        "memory_percent": memory,
        "gpu_percent": gpu,
        "cpu_temperature_c": cpu_temp,
        "gpu_temperature_c": gpu_temp,
    }


def _quota_window(label, remaining, resets_at=None):
    return {"label": label, "remaining_percent": remaining, "resets_at": resets_at}


class AgentHttpTest(unittest.TestCase):
    def setUp(self) -> None:
        self.collector = PcMetricsCollector(collect_fn=_sample)
        self.collector.run_once()
        self.server = build_server(self.collector, "127.0.0.1", 0)
        self.server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.server_thread.start()
        self.base = "http://127.0.0.1:%d" % self.server.server_address[1]

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.server_thread.join(timeout=5)

    def _get(self, path: str, method: str = "GET") -> tuple:
        request = urllib.request.Request(self.base + path, method=method)
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                return response.status, response.headers, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.headers, error.read()

    def test_health_contract(self) -> None:
        status, headers, body = self._get("/api/v1/health")
        self.assertEqual(status, 200)
        self.assertEqual(headers["Content-Type"], "application/json; charset=utf-8")
        self.assertEqual(int(headers["Content-Length"]), len(body))
        payload = json.loads(body)
        self.assertEqual(payload, build_health_payload())
        self.assertEqual(
            payload["data"]["capabilities"],
            ["pc.metrics", "quotas.codex", "quotas.zhipu"],
        )

    def test_metrics_contract(self) -> None:
        status, _, body = self._get("/api/v1/pc/metrics")
        self.assertEqual(status, 200)
        payload = json.loads(body)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["status"], "ok")
        self.assertIsNotNone(payload["updated_at_epoch"])
        self.assertIsInstance(payload["age_sec"], (int, float))
        self.assertEqual(payload["data"]["cpu_percent"], 23.4)
        self.assertIsNone(payload["error_code"])

    def test_unknown_path_is_404(self) -> None:
        status, _, body = self._get("/api/v1/nope")
        self.assertEqual(status, 404)
        payload = json.loads(body)
        self.assertEqual(payload["status"], "not_found")

    def test_dashboard_style_path_is_404(self) -> None:
        status, _, _ = self._get("/dashboard")
        self.assertEqual(status, 404)

    def test_post_is_405(self) -> None:
        status, _, _ = self._get("/api/v1/pc/metrics", method="POST")
        self.assertEqual(status, 405)

    def test_unavailable_collector_still_answers_200(self) -> None:
        empty = PcMetricsCollector(collect_fn=lambda: _sample(cpu=None))
        self.server.collector = empty
        status, _, body = self._get("/api/v1/pc/metrics")
        self.assertEqual(status, 200)
        payload = json.loads(body)
        self.assertEqual(payload["status"], "unavailable")
        self.assertIsNone(payload["data"]["cpu_percent"])

    def test_concurrent_requests(self) -> None:
        results = []
        lock = threading.Lock()

        def hammer():
            for _ in range(10):
                status, _, body = self._get("/api/v1/pc/metrics")
                payload = json.loads(body)
                with lock:
                    results.append((status, payload["data"]["cpu_percent"]))

        threads = [threading.Thread(target=hammer) for _ in range(20)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(len(results), 200)
        self.assertTrue(all(status == 200 and cpu == 23.4 for status, cpu in results))


class QuotaHttpTest(unittest.TestCase):
    """Quota routes: contract, independence and absence of secrets."""

    def setUp(self) -> None:
        self.collector = PcMetricsCollector(collect_fn=_sample)
        self.collector.run_once()
        self.codex = QuotaCache(
            "codex",
            collect_fn=lambda: {
                "plan": "plus",
                "windows": [_quota_window("5H", 80, 1_800_000_000), _quota_window("7D", 12, None)],
            },
        )
        self.zhipu = QuotaCache(
            "zhipu",
            collect_fn=lambda: {
                "plan": None,
                "windows": [_quota_window("5H", 64, 1_800_000_000), _quota_window("1W", 28, 1_800_300_000)],
            },
        )
        self.codex.run_once()
        self.zhipu.run_once()
        self.server = build_server(
            self.collector,
            "127.0.0.1",
            0,
            quotas={"codex": self.codex, "zhipu": self.zhipu},
        )
        self.server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.server_thread.start()
        self.base = "http://127.0.0.1:%d" % self.server.server_address[1]

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.server_thread.join(timeout=5)

    def _get(self, path: str, method: str = "GET") -> tuple:
        request = urllib.request.Request(self.base + path, method=method)
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                return response.status, response.headers, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.headers, error.read()

    def test_codex_contract(self) -> None:
        status, _, body = self._get("/api/v1/quotas/codex")
        self.assertEqual(status, 200)
        payload = json.loads(body)
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["status"], STATUS_OK)
        self.assertIsNone(payload["error_code"])
        data = payload["data"]
        self.assertEqual(data["provider_id"], "codex")
        self.assertEqual(data["plan"], "plus")
        self.assertIsNotNone(data["updated_at_epoch"])
        self.assertEqual([window["label"] for window in data["windows"]], ["5H", "7D"])
        self.assertEqual(
            sorted(data["windows"][0].keys()),
            ["label", "remaining_percent", "reset_at_local", "reset_in_sec", "resets_at"],
        )
        self.assertEqual(data["windows"][0]["remaining_percent"], 80)
        self.assertIsNone(data["windows"][1]["resets_at"])

    def test_zhipu_contract_uses_its_own_windows(self) -> None:
        status, _, body = self._get("/api/v1/quotas/zhipu")
        self.assertEqual(status, 200)
        payload = json.loads(body)
        self.assertEqual(payload["data"]["provider_id"], "zhipu")
        self.assertEqual([window["label"] for window in payload["data"]["windows"]], ["5H", "1W"])

    def test_unknown_quota_provider_is_404(self) -> None:
        status, _, body = self._get("/api/v1/quotas/other")
        self.assertEqual(status, 404)
        self.assertEqual(json.loads(body)["status"], "not_found")

    def test_quota_route_without_scheduler_is_unavailable(self) -> None:
        self.server.quotas = {"codex": self.codex}
        status, _, body = self._get("/api/v1/quotas/zhipu")
        self.assertEqual(status, 200)
        payload = json.loads(body)
        self.assertEqual(payload["status"], "unavailable")
        self.assertEqual(payload["error_code"], "no_snapshot")

    def test_provider_failure_does_not_touch_the_other(self) -> None:
        def failing():
            raise QuotaError(STATUS_AUTH_REQUIRED, ERROR_AUTH_REQUIRED)

        self.zhipu._collect_fn = failing
        self.zhipu.run_once()
        codex_payload = json.loads(self._get("/api/v1/quotas/codex")[2])
        zhipu_payload = json.loads(self._get("/api/v1/quotas/zhipu")[2])
        self.assertEqual(codex_payload["status"], STATUS_OK)
        self.assertEqual(zhipu_payload["status"], STATUS_AUTH_REQUIRED)
        self.assertTrue(zhipu_payload["data"]["windows"])

    def test_invalid_provider_data_is_reported(self) -> None:
        def broken():
            raise QuotaError(STATUS_INVALID_DATA, ERROR_INVALID_DATA)

        self.codex._collect_fn = broken
        self.codex.run_once()
        payload = json.loads(self._get("/api/v1/quotas/codex")[2])
        self.assertEqual(payload["status"], STATUS_INVALID_DATA)
        self.assertEqual(payload["error_code"], ERROR_INVALID_DATA)

    def test_metrics_route_still_works_with_quotas_attached(self) -> None:
        status, _, body = self._get("/api/v1/pc/metrics")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["data"]["cpu_percent"], 23.4)

    def test_post_to_quota_route_is_405(self) -> None:
        status, _, _ = self._get("/api/v1/quotas/codex", method="POST")
        self.assertEqual(status, 405)

    def test_quota_routes_only_read_the_cache(self) -> None:
        """The HTTP path never triggers an upstream query (goal decision 2)."""
        calls = {"count": 0}

        def collect():
            calls["count"] += 1
            return {"plan": None, "windows": [_quota_window("5H", 50)]}

        cache = QuotaCache("codex", collect_fn=collect)
        self.server.quotas = {"codex": cache}
        for _ in range(5):
            status, _, _ = self._get("/api/v1/quotas/codex")
            self.assertEqual(status, 200)
        self.assertEqual(calls["count"], 0)
        self.assertTrue(cache.run_once())
        self.assertEqual(calls["count"], 1)

    def test_quota_response_carries_no_credentials(self) -> None:
        """A real normalization round must not surface the token anywhere."""
        from zhipu_quota import ZhipuCollector, ZhipuCredentials

        fake_token = "test-token-not-real"
        collector = ZhipuCollector(
            config_loader=lambda: ZhipuCredentials(
                "https://open.bigmodel.cn/api/anthropic", fake_token, "environment"
            ),
            fetch_fn=lambda url, token: {
                "data": {
                    "level": "pro",
                    "limits": [
                        {
                            "type": "TOKENS_LIMIT",
                            "unit": 3,
                            "percentage": 36,
                            "nextResetTime": 1_800_000_000_000,
                        }
                    ],
                }
            },
        )
        cache = QuotaCache("zhipu", collect_fn=collector.collect)
        self.assertTrue(cache.run_once())
        self.server.quotas = {"zhipu": cache}

        status, _, body = self._get("/api/v1/quotas/zhipu")
        self.assertEqual(status, 200)
        text = body.decode()
        self.assertNotIn(fake_token, text)
        for secret_key in ("Authorization", "apiKey", "baseUrl", "token"):
            self.assertNotIn(secret_key, text)

        payload = json.loads(body)
        self.assertEqual(payload["status"], STATUS_OK)
        self.assertEqual(payload["data"]["updated_at_epoch"] is not None, True)
        window = payload["data"]["windows"][0]
        self.assertEqual(
            sorted(window.keys()),
            ["label", "remaining_percent", "reset_at_local", "reset_in_sec", "resets_at"],
        )
        self.assertEqual(window["remaining_percent"], 64)


if __name__ == "__main__":
    unittest.main()