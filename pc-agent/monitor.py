"""Xiaomiao Agent: unified PC-side HTTP backend (API v1, goal node 11).

One backend, one address, one port (goal decision 1). PC metrics live
under ``/api/v1/pc/metrics``; later businesses such as AI quotas get
their own ``/api/v1/...`` routes instead of a second server. The HTTP
layer only serializes the collector's cached snapshot and never runs a
query itself (goal decision 5).

The HTTP API listens on ``0.0.0.0:8766`` by default and offers read-only
GET routes. A UDP responder on port 8767 lets devices discover the
current LAN address and HTTP port (goal decision 11 and service discovery
follow-up).
"""

from __future__ import annotations

import argparse
import json
import socketserver
import struct
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread
from typing import Optional

from pc_metrics import CollectorWorker, PcMetricsCollector

AGENT_NAME = "xiaomiao-agent"
AGENT_VERSION = "0.1.0"
API_VERSION = 1

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8766
DISCOVERY_HOST = "0.0.0.0"
DISCOVERY_PORT = 8767
DISCOVERY_REQUEST = b"XIAOMIAO_AGENT_DISCOVER_V1"
DISCOVERY_RESPONSE_MAGIC = b"XMA1"

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


def build_discovery_response(http_port: int) -> bytes:
    if not 1 <= http_port <= 65535:
        raise ValueError("HTTP port must be between 1 and 65535")
    return DISCOVERY_RESPONSE_MAGIC + struct.pack("!H", http_port)


class AgentDiscoveryRequestHandler(socketserver.BaseRequestHandler):
    """Reply to the discovery protocol with the actual HTTP port."""

    def handle(self) -> None:
        request, response_socket = self.request
        if request != DISCOVERY_REQUEST:
            return
        response_socket.sendto(
            build_discovery_response(self.server.http_port), self.client_address
        )


class AgentDiscoveryServer(socketserver.UDPServer):
    allow_reuse_address = True

    def __init__(
        self, http_port: int, host: str = DISCOVERY_HOST, port: int = DISCOVERY_PORT
    ) -> None:
        self.http_port = http_port
        super().__init__((host, port), AgentDiscoveryRequestHandler)


class AgentHttpServer(ThreadingHTTPServer):
    request_queue_size = 64


def build_server(collector: PcMetricsCollector, host: str, port: int) -> AgentHttpServer:
    server = AgentHttpServer((host, port), AgentRequestHandler)
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
    try:
        discovery_server = AgentDiscoveryServer(server.server_port, args.host)
    except OSError as error:
        sys.stderr.write(
            "xiaomiao-agent: cannot bind UDP discovery %s:%d (%s)\n"
            % (DISCOVERY_HOST, DISCOVERY_PORT, error)
        )
        server.server_close()
        worker.stop()
        return 1
    discovery_thread = Thread(
        target=discovery_server.serve_forever,
        name="xiaomiao-agent-discovery",
        daemon=True,
    )
    discovery_thread.start()
    print(
        "xiaomiao-agent %s listening on %s:%d, capabilities: %s"
        % (AGENT_VERSION, args.host, server.server_port, ",".join(CAPABILITIES)),
        flush=True,
    )
    print(
        "xiaomiao-agent discovery listening on %s:%d/udp"
        % discovery_server.server_address,
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        discovery_server.shutdown()
        discovery_server.server_close()
        discovery_thread.join(timeout=2)
        server.server_close()
        worker.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
