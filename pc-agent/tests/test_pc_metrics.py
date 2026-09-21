"""Unit tests for the PC metrics collector and snapshot semantics."""

import os
import shutil
import sys
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from pc_metrics import (  # noqa: E402
    ERROR_HARDWARE_UNAVAILABLE,
    ERROR_NO_METRICS,
    ERROR_STALE,
    STATUS_DEGRADED,
    STATUS_OK,
    STATUS_STALE,
    STATUS_UNAVAILABLE,
    PcMetricsCollector,
)


def _sample(cpu=10.0, memory=20.0, gpu=30.0, cpu_temp=45.0, gpu_temp=40.0):
    return {
        "cpu_percent": cpu,
        "memory_percent": memory,
        "gpu_percent": gpu,
        "cpu_temperature_c": cpu_temp,
        "gpu_temperature_c": gpu_temp,
    }


class _Clock:
    """Controllable monotonic clock shared with the collector."""

    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


class PcMetricsCollectorTest(unittest.TestCase):
    def setUp(self) -> None:
        self.clock = _Clock()
        self.epoch_value = 1790000000
        self.collector = PcMetricsCollector(
            collect_fn=_sample,
            monotonic=self.clock,
            epoch=lambda: self.epoch_value,
        )

    def test_ok_when_every_field_present(self) -> None:
        self.collector.run_once()
        snapshot = self.collector.snapshot()
        self.assertEqual(snapshot["status"], STATUS_OK)
        self.assertIsNone(snapshot["error_code"])
        self.assertEqual(snapshot["data"]["cpu_percent"], 10.0)
        self.assertEqual(snapshot["data"]["gpu_temperature_c"], 40.0)
        self.assertEqual(snapshot["updated_at_epoch"], self.epoch_value)
        self.assertEqual(snapshot["age_sec"], 0)

    def test_degraded_when_optional_missing(self) -> None:
        self.collector = PcMetricsCollector(
            collect_fn=lambda: _sample(gpu=None, cpu_temp=None, gpu_temp=None),
            monotonic=self.clock,
            epoch=lambda: self.epoch_value,
        )
        self.collector.run_once()
        snapshot = self.collector.snapshot()
        self.assertEqual(snapshot["status"], STATUS_DEGRADED)
        self.assertEqual(snapshot["error_code"], ERROR_HARDWARE_UNAVAILABLE)
        self.assertIsNone(snapshot["data"]["gpu_percent"])
        self.assertEqual(snapshot["data"]["memory_percent"], 20.0)

    def test_unavailable_without_any_success(self) -> None:
        self.collector = PcMetricsCollector(
            collect_fn=lambda: _sample(cpu=None),
            monotonic=self.clock,
            epoch=lambda: self.epoch_value,
        )
        self.collector.run_once()
        snapshot = self.collector.snapshot()
        self.assertEqual(snapshot["status"], STATUS_UNAVAILABLE)
        self.assertEqual(snapshot["error_code"], ERROR_NO_METRICS)
        self.assertIsNone(snapshot["updated_at_epoch"])
        self.assertIsNone(snapshot["data"]["cpu_percent"])

    def test_stale_after_freshness_window(self) -> None:
        self.collector.run_once()
        self.clock.now += 10.5
        snapshot = self.collector.snapshot()
        self.assertEqual(snapshot["status"], STATUS_STALE)
        self.assertEqual(snapshot["error_code"], ERROR_STALE)
        self.assertEqual(snapshot["data"]["cpu_percent"], 10.0)
        self.assertEqual(snapshot["age_sec"], 10.5)

    def test_core_failure_keeps_last_good_data(self) -> None:
        self.collector.run_once()
        self.collector._collect_fn = lambda: _sample(cpu=None)
        self.collector.run_once()
        snapshot = self.collector.snapshot()
        self.assertEqual(snapshot["data"]["cpu_percent"], 10.0)
        self.assertIn(
            snapshot["status"],
            (STATUS_OK, STATUS_DEGRADED, STATUS_STALE),
        )

    def test_provider_exception_does_not_raise(self) -> None:
        def broken():
            raise RuntimeError("broken provider")

        self.collector = PcMetricsCollector(
            collect_fn=broken,
            monotonic=self.clock,
            epoch=lambda: self.epoch_value,
        )
        self.assertFalse(self.collector.run_once())
        self.assertEqual(self.collector.snapshot()["status"], STATUS_UNAVAILABLE)

    def test_invalid_values_are_rejected(self) -> None:
        self.collector = PcMetricsCollector(
            collect_fn=lambda: _sample(cpu=150.0, memory=float("nan"), gpu=-3),
            monotonic=self.clock,
            epoch=lambda: self.epoch_value,
        )
        self.collector.run_once()
        self.assertEqual(self.collector.snapshot()["status"], STATUS_UNAVAILABLE)

    def test_zero_is_a_real_value(self) -> None:
        self.collector = PcMetricsCollector(
            collect_fn=lambda: _sample(cpu=0.0, gpu=0.0, gpu_temp=0.0),
            monotonic=self.clock,
            epoch=lambda: self.epoch_value,
        )
        self.collector.run_once()
        snapshot = self.collector.snapshot()
        self.assertEqual(snapshot["status"], STATUS_OK)
        self.assertEqual(snapshot["data"]["cpu_percent"], 0.0)

    def test_snapshot_returns_a_private_copy(self) -> None:
        self.collector.run_once()
        first = self.collector.snapshot()
        first["data"]["cpu_percent"] = 999.0
        second = self.collector.snapshot()
        self.assertEqual(second["data"]["cpu_percent"], 10.0)

    def test_concurrent_reads_are_consistent(self) -> None:
        self.collector.run_once()
        errors = []

        def hammer():
            for _ in range(200):
                snapshot = self.collector.snapshot()
                data = snapshot["data"]
                if data["cpu_percent"] not in (None, 10.0):
                    errors.append(data)
                self.clock.now += 0.001

        threads = [threading.Thread(target=hammer) for _ in range(8)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(errors, [])


class NvidiaSmiIntegrationTest(unittest.TestCase):
    @unittest.skipUnless(
        shutil.which("nvidia-smi") is not None,
        "nvidia-smi not available on this host",
    )
    def test_real_nvidia_smi_returns_numbers(self) -> None:
        from pc_metrics import _nvidia_smi_sample
        utilization, temperature = _nvidia_smi_sample()
        self.assertIsNotNone(utilization)
        self.assertIsNotNone(temperature)
        self.assertGreaterEqual(utilization, 0.0)
        self.assertLessEqual(utilization, 100.0)
        self.assertGreaterEqual(temperature, -100.0)
        self.assertLessEqual(temperature, 150.0)


if __name__ == "__main__":
    unittest.main()