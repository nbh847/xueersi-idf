"""Xiaomiao Agent: unified PC-side HTTP backend (API v1, goal node 11).

One backend, one address, one port (goal decision 1). PC metrics live
under ``/api/v1/pc/metrics``; later businesses such as AI quotas get
their own ``/api/v1/...`` routes instead of a second server. The HTTP
layer only serializes the collector's cached snapshot and never runs a
query itself (goal decision 5).

The first version listens on ``0.0.0.0:8766``, offers read-only GET
routes and is meant for a trusted LAN only (goal decision 11).
"""

from __future__ import annotations

import argparse
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Optional, Tuple

from pc_metrics import CollectorWorker, PcMetricsCollector

AGENT_NAME = "xiaomiao-agent"
AGENT_VERSION = "0.1.0"
API_VERSION = 1

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8766

CONTENT_TYPE_JSON = "application/json; charset=utf-8"

# Capabilities may only list what this build really serves (goal contract
# for /api/v1/health).
CAPABILITIES = ("pc.metrics",)


def build_health_payload() -> dict:
    return {
        "schema_version": 1,
        "status": "ok",
        "data": {
            "agent_name": AGENT_NAME,
            "agent_version": AGENT_VERSION,
            "api_version": API_VERSION,
            "capabilities": list(CAPABILITIES),
        },
        "error_code": None,
    }


def build_error_payload(status: str, error_code: str) -> dict:
    return {
        "schema_version": 1,
        "status": status,
        "data": None,
        "error_code": error_code,
    }


class AgentRequestHandler(BaseHTTPRequestHandler):
    """Read-only GET routes; unknown paths are 404, other methods 405."""

    server_version = AGENT_NAME + "/" + AGENT_VERSION
    protocol_version = "HTTP/1.1"

    def do_GET(self) -> None:  # noqa: N802 - http.server naming
        path = self.path.split("?", 1)[0]
        if path == "/api/v1/health":
            self._send_json(build_health_payload())
        elif path == "/api/v1/pc/metrics":
            self._send_json(self.server.collector.snapshot())
        else:
            self._send_json(build_error_payload("not_found", "unknown_path"), status=404)

    def _reject_method(self) -> None:
        self._send_json(build_error_payload("method_not_allowed", "get_only"), status=405)

    do_POST = _reject_method
    do_PUT = _reject_method
    do_DELETE = _reject_method
    do_PATCH = _reject_method
    do_HEAD = _reject_method

    def _send_json(self, payload: dict, status: int = 200) -> None:
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", CONTENT_TYPE_JSON)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def log_message(self, format: str, *args) -> None:  # noqa: A002
        # One short line per request; no secrets or raw bodies exist here.
        sys.stderr.write("[%s] %s\n" % (self.log_date_time_string(), format % args))


def build_server(collector: PcMetricsCollector, host: str, port: int) -> ThreadingHTTPServer:
    server = ThreadingHTTPServer((host, port), AgentRequestHandler)
    server.collector = collector  # type: ignore[attr-defined]
    server.daemon_threads = True
    return server


def parse_args(argv: Optional[list] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Xiaomiao Agent HTTP backend")
    parser.add_argument("--host", default=DEFAULT_HOST, help="bind address (default %(default)s)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="TCP port (default %(default)s)")
    return parser.parse_args(argv)


def main(argv: Optional[list] = None) -> int:
    args = parse_args(argv)
    collector = PcMetricsCollector()
    worker = CollectorWorker(collector)
    worker.start()
    try:
        server = build_server(collector, args.host, args.port)
    except OSError as error:
        sys.stderr.write("xiaomiao-agent: cannot bind %s:%d (%s)\n" % (args.host, args.port, error))
        worker.stop()
        return 1
    print(
        "xiaomiao-agent %s listening on %s:%d, capabilities: %s"
        % (AGENT_VERSION, args.host, args.port, ",".join(CAPABILITIES)),
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        worker.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())