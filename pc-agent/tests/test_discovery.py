"""LAN discovery protocol tests for the Xiaomiao Agent."""

import os
import socket
import struct
import sys
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from monitor import (  # noqa: E402
    AgentDiscoveryServer,
    DISCOVERY_REQUEST,
    build_discovery_response,
)


class AgentDiscoveryTest(unittest.TestCase):
    def test_response_encodes_http_port_as_big_endian(self) -> None:
        self.assertEqual(
            build_discovery_response(8766), b"XMA1" + struct.pack("!H", 8766)
        )
        for port in (0, -1, 65536):
            with self.subTest(port=port), self.assertRaises(ValueError):
                build_discovery_response(port)

    def test_responder_ignores_other_requests_and_replies_to_discovery(self) -> None:
        server = AgentDiscoveryServer(43210, "127.0.0.1", 0)
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client.settimeout(0.2)
        try:
            target = server.server_address
            client.sendto(b"not-a-discovery-request", target)
            with self.assertRaises(socket.timeout):
                client.recvfrom(64)

            client.sendto(DISCOVERY_REQUEST, target)
            response, source = client.recvfrom(64)
            self.assertEqual(response, b"XMA1" + struct.pack("!H", 43210))
            self.assertEqual(source[0], "127.0.0.1")
        finally:
            client.close()
            server.shutdown()
            server.server_close()
            server_thread.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
