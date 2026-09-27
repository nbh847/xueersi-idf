"""Tests for the shared quota cache and helpers (AI quota pages goal)."""

import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from quota_cache import (  # noqa: E402
    ERROR_AUTH_REQUIRED,
    ERROR_INVALID_DATA,
    ERROR_NO_SNAPSHOT,
    ERROR_STALE,
    ERROR_UNAVAILABLE,
    STATUS_AUTH_REQUIRED,
    STATUS_INVALID_DATA,
    STATUS_OK,
    STATUS_STALE,
    STATUS_UNAVAILABLE,
    QuotaCache,
    QuotaError,
    QuotaWorker,
    finite_number,
    remaining_percent,
    valid_timestamp_seconds,
    window_payload,
)

LOCAL_TIME_PATTERN = re.compile(r"^\d{2}-\d{2} \d{2}:\d{2}$")


def _window(label="5H", remaining=64, resets_at=1_800_000_000):
    return {"label": label, "remaining_percent": remaining, "resets_at": resets_at}


class RemainingPercentTest(unittest.TestCase):
    def test_full_remaining_when_nothing_used(self) -> None:
        self.assertEqual(remaining_percent(0), 100)

    def test_zero_percent_is_a_real_value(self) -> None:
        self.assertEqual(remaining_percent(100), 0)
        self.assertEqual(remaining_percent(100.0), 0)
        self.assertEqual(remaining_percent(140), 0)

    def test_clamps_negative_used_to_full(self) -> None:
        self.assertEqual(remaining_percent(-5), 100)

    def test_rounds_to_nearest_integer(self) -> None:
        self.assertEqual(remaining_percent(0.4), 100)
        self.assertEqual(remaining_percent(0.6), 99)

    def test_rejects_non_numbers(self) -> None:
        for value in (True, False, "64", None, float("nan"), float("inf")):
            with self.subTest(value=value):
                self.assertIsNone(remaining_percent(value))

    def test_finite_number_rejects_bool(self) -> None:
        self.assertIsNone(finite_number(True))
        self.assertEqual(finite_number(64), 64.0)


class TimestampTest(unittest.TestCase):
    def test_positive_integer_is_accepted(self) -> None:
        self.assertEqual(valid_timestamp_seconds(1_800_000_000), 1_800_000_000)

    def test_invalid_timestamps_are_unknown(self) -> None:
        for value in (0, -1, True, "1800000000", 1.5, None):
            with self.subTest(value=value):
                self.assertIsNone(valid_timestamp_seconds(value))

    def test_window_payload_contract_and_clamp(self) -> None:
        payload = window_payload("5H", 0, 1_800_000_000, 1_800_000_600)
        self.assertEqual(payload["remaining_percent"], 0)
        self.assertEqual(payload["reset_in_sec"], 0)
        self.assertRegex(payload["reset_at_local"], LOCAL_TIME_PATTERN)

    def test_window_payload_unknown_reset_time(self) -> None:
        payload = window_payload("1W", 28, None, 1_800_000_600)
        self.assertIsNone(payload["reset_in_sec"])
        self.assertIsNone(payload["reset_at_local"])


class QuotaCacheTest(unittest.TestCase):
    def _cache(self, **kwargs) -> QuotaCache:
        self._now_mono = [100.0]
        self._now_epoch = [1_800_000_000.0]
        defaults = dict(
            monotonic=lambda: self._now_mono[0],
            epoch=lambda: self._now_epoch[0],
        )
        defaults.update(kwargs)
        return QuotaCache("zhipu", **defaults)

    def test_empty_snapshot_contract(self) -> None:
        snapshot = self._cache().snapshot()
        self.assertEqual(snapshot["schema_version"], 1)
        self.assertEqual(snapshot["status"], STATUS_UNAVAILABLE)
        self.assertEqual(snapshot["error_code"], ERROR_NO_SNAPSHOT)
        self.assertEqual(
            snapshot["data"],
            {
                "provider_id": "zhipu",
                "plan": None,
                "windows": [],
                "updated_at_epoch": None,
            },
        )

    def test_store_renders_live_window(self) -> None:
        cache = self._cache()
        cache.store("pro", [_window(resets_at=1_800_000_300)])
        snapshot = cache.snapshot()
        self.assertEqual(snapshot["status"], STATUS_OK)
        self.assertIsNone(snapshot["error_code"])
        self.assertEqual(snapshot["data"]["plan"], "pro")
        self.assertEqual(snapshot["data"]["updated_at_epoch"], 1_800_000_000)
        window = snapshot["data"]["windows"][0]
        self.assertEqual(window["label"], "5H")
        self.assertEqual(window["remaining_percent"], 64)
        self.assertEqual(window["resets_at"], 1_800_000_300)
        self.assertEqual(window["reset_in_sec"], 300)
        self.assertRegex(window["reset_at_local"], LOCAL_TIME_PATTERN)

    def test_snapshot_ages_into_stale_but_keeps_values(self) -> None:
        cache = self._cache()
        cache.store(None, [_window()])
        self._now_mono[0] += 179.9
        self.assertEqual(cache.snapshot()["status"], STATUS_OK)
        self._now_mono[0] += 1.0
        snapshot = cache.snapshot()
        self.assertEqual(snapshot["status"], STATUS_STALE)
        self.assertEqual(snapshot["error_code"], ERROR_STALE)
        self.assertEqual(snapshot["data"]["windows"][0]["remaining_percent"], 64)

    def test_auth_required_with_old_snapshot_is_not_rewritten_as_stale(self) -> None:
        cache = self._cache()
        cache.store(None, [_window()])
        cache.set_failure(STATUS_AUTH_REQUIRED, ERROR_AUTH_REQUIRED)
        self._now_mono[0] += 10_000
        snapshot = cache.snapshot()
        self.assertEqual(snapshot["status"], STATUS_AUTH_REQUIRED)
        self.assertEqual(snapshot["error_code"], ERROR_AUTH_REQUIRED)
        self.assertTrue(snapshot["data"]["windows"])

    def test_run_once_stores_collector_result(self) -> None:
        cache = self._cache(collect_fn=lambda: {"plan": None, "windows": [_window()]})
        self.assertTrue(cache.run_once())
        self.assertEqual(cache.snapshot()["status"], STATUS_OK)

    def test_run_once_maps_quota_error_and_keeps_old_values(self) -> None:
        calls = {"count": 0}

        def collect():
            calls["count"] += 1
            if calls["count"] == 2:
                raise QuotaError(STATUS_INVALID_DATA, ERROR_INVALID_DATA)
            return {"plan": None, "windows": [_window()]}

        cache = self._cache(collect_fn=collect)
        self.assertTrue(cache.run_once())
        self.assertFalse(cache.run_once())
        snapshot = cache.snapshot()
        self.assertEqual(snapshot["status"], STATUS_INVALID_DATA)
        self.assertEqual(snapshot["data"]["windows"][0]["remaining_percent"], 64)

    def test_run_once_contains_unexpected_exception(self) -> None:
        def collect():
            raise RuntimeError("boom")

        cache = self._cache(collect_fn=collect)
        self.assertFalse(cache.run_once())
        self.assertEqual(cache.snapshot()["status"], STATUS_UNAVAILABLE)
        self.assertEqual(cache.snapshot()["error_code"], ERROR_UNAVAILABLE)

    def test_run_once_rejects_empty_windows(self) -> None:
        cache = self._cache(collect_fn=lambda: {"plan": None, "windows": []})
        self.assertFalse(cache.run_once())
        self.assertEqual(cache.snapshot()["status"], STATUS_INVALID_DATA)

    def test_run_once_requires_collect_fn(self) -> None:
        with self.assertRaises(RuntimeError):
            self._cache().run_once()

    def test_recovery_after_failure_returns_to_ok_with_new_values(self) -> None:
        """A failed round must not stick: the next success clears it."""
        sequence = {"round": 0}

        def collect():
            sequence["round"] += 1
            if sequence["round"] == 1:
                return {"plan": None, "windows": [_window(remaining=64)]}
            if sequence["round"] == 2:
                raise QuotaError(STATUS_AUTH_REQUIRED, ERROR_AUTH_REQUIRED)
            return {"plan": "pro", "windows": [_window(remaining=55)]}

        cache = self._cache(collect_fn=collect)
        self.assertTrue(cache.run_once())
        self._now_mono[0] += 1.0
        self.assertFalse(cache.run_once())
        failed = cache.snapshot()
        self.assertEqual(failed["status"], STATUS_AUTH_REQUIRED)
        self.assertEqual(failed["data"]["windows"][0]["remaining_percent"], 64)

        self._now_mono[0] += 1.0
        self.assertTrue(cache.run_once())
        recovered = cache.snapshot()
        self.assertEqual(recovered["status"], STATUS_OK)
        self.assertIsNone(recovered["error_code"])
        self.assertEqual(recovered["data"]["plan"], "pro")
        self.assertEqual(recovered["data"]["windows"][0]["remaining_percent"], 55)

    def test_all_windows_share_one_time_sample(self) -> None:
        """render uses a single epoch sample for every window and countdown."""
        ticks = iter([1_800_000_000.0, 1_800_000_100.0, 1_800_000_200.0])
        cache = QuotaCache("zhipu", monotonic=lambda: 0.0, epoch=lambda: next(ticks))
        cache.store(
            None,
            [
                {"label": "5H", "remaining_percent": 64, "resets_at": 1_800_000_500},
                {"label": "1W", "remaining_percent": 28, "resets_at": 1_800_000_900},
            ],
        )
        windows = cache.snapshot()["data"]["windows"]
        # store() consumed the first tick; every window must use the second.
        self.assertEqual(windows[0]["reset_in_sec"], 1_800_000_500 - 1_800_000_100)
        self.assertEqual(windows[1]["reset_in_sec"], 1_800_000_900 - 1_800_000_100)

    def test_snapshot_copy_is_not_shared_state(self) -> None:
        cache = self._cache()
        cache.store(None, [_window()])
        first = cache.snapshot()
        first["data"]["windows"][0]["remaining_percent"] = 1
        self.assertEqual(cache.snapshot()["data"]["windows"][0]["remaining_percent"], 64)


class QuotaWorkerTest(unittest.TestCase):
    @staticmethod
    def _cache() -> QuotaCache:
        return QuotaCache("zhipu", collect_fn=lambda: {"plan": None, "windows": [_window()]})

    def test_worker_runs_one_round_and_stops(self) -> None:
        cache = self._cache()
        worker = QuotaWorker(cache, period_seconds=0.05)
        worker.start()
        worker.stop()
        self.assertEqual(cache.snapshot()["status"], STATUS_OK)


if __name__ == "__main__":
    unittest.main()
