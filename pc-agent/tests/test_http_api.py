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


def _sample(cpu=23.4, memory=61.2, gpu=72.0, cpu_temp=55.0, gpu_temp=64.0):
    return {
        "cpu_percent": cpu,
        "memory_percent": memory,
        "gpu_percent": gpu,
        "cpu_temperature_c": cpu_temp,
        "gpu_temperature_c": gpu_temp,
    }


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


if __name__ == "__main__":
    unittest.main()