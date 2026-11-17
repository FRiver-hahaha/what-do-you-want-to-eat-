"""40-byte framed TCP client shared by the Agent and integration tests."""

from __future__ import annotations

import json
import socket
import struct
import threading
from typing import Any

MAGIC = 0x45415431
VERSION_MAJOR = 1
VERSION_MINOR = 0
HEADER_SIZE = 40
MAX_FRAME_SIZE = 65536

FRAME_HANDSHAKE_REQ = 0x01
FRAME_HANDSHAKE_RESP = 0x02
FRAME_REQUEST = 0x03
FRAME_RESPONSE = 0x04
FRAME_HEARTBEAT = 0x05
FRAME_HEARTBEAT_ACK = 0x06
FRAME_ERROR = 0x09

_POLYNOMIAL = 0x82F63B78


def _build_crc_table() -> list[int]:
    table = []
    for index in range(256):
        crc = index
        for _ in range(8):
            crc = (crc >> 1) ^ (_POLYNOMIAL if crc & 1 else 0)
        table.append(crc & 0xFFFFFFFF)
    return table


_CRC_TABLE = _build_crc_table()


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    table = _CRC_TABLE
    for byte in data:
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


def encode_frame(frame_type: int, request_id: int, body: bytes,
                 seq: int = 0, error: int = 0, flags: int = 0) -> bytes:
    header = bytearray(HEADER_SIZE)
    struct.pack_into(">I", header, 0, MAGIC)
    header[4] = VERSION_MAJOR
    header[5] = VERSION_MINOR
    header[6] = frame_type
    header[7] = flags
    header[8] = HEADER_SIZE
    header[9] = 0
    struct.pack_into(">I", header, 10, len(body))
    struct.pack_into(">Q", header, 14, request_id)
    struct.pack_into(">Q", header, 22, seq)
    struct.pack_into(">I", header, 30, error)
    struct.pack_into(">H", header, 34, 0)
    struct.pack_into(">I", header, 36, crc32c(bytes(header[:36]) + body))
    return bytes(header) + body


class TcpFrameError(RuntimeError):
    pass


class TcpFrameClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 9443,
                 timeout: float = 3.0) -> None:
        self.host = host
        self.port = port
        self.timeout = timeout
        self.user_key = ""
        self.access_token = ""
        self._socket: socket.socket | None = None
        self._lock = threading.Lock()
        self._next_id = 1

    def __enter__(self) -> "TcpFrameClient":
        self.connect()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def connect(self) -> None:
        if self._socket is not None:
            return
        sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.settimeout(self.timeout)
        self._socket = sock

    def close(self) -> None:
        if self._socket is not None:
            try:
                self._socket.close()
            finally:
                self._socket = None

    def _recv_exact(self, size: int) -> bytes:
        assert self._socket is not None
        chunks = bytearray()
        while len(chunks) < size:
            chunk = self._socket.recv(size - len(chunks))
            if not chunk:
                raise TcpFrameError("connection_closed")
            chunks.extend(chunk)
        return bytes(chunks)

    def _read_frame(self) -> tuple[int, int, int, bytes]:
        header = self._recv_exact(HEADER_SIZE)
        magic = struct.unpack_from(">I", header, 0)[0]
        if magic != MAGIC:
            raise TcpFrameError("invalid_magic")
        frame_type = header[6]
        body_len = struct.unpack_from(">I", header, 10)[0]
        request_id = struct.unpack_from(">Q", header, 14)[0]
        checksum = struct.unpack_from(">I", header, 36)[0]
        if body_len > MAX_FRAME_SIZE:
            raise TcpFrameError("frame_too_large")
        body = self._recv_exact(body_len) if body_len else b""
        if crc32c(header[:36] + body) != checksum:
            raise TcpFrameError("checksum_failed")
        return frame_type, request_id, struct.unpack_from(">I", header, 30)[0], body

    def _send(self, frame_type: int, request_id: int, body: dict[str, Any] | None = None) -> None:
        assert self._socket is not None
        payload = json.dumps(body or {}, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._socket.sendall(encode_frame(frame_type, request_id, payload))

    def handshake(self, *, access_token: str = "", login_id: str = "",
                  password: str = "", register: bool = False,
                  display_name: str = "", device_id: str = "eat-agent",
                  client_version: str = "1.0.0") -> dict[str, Any]:
        with self._lock:
            request_id = self._next_id
            self._next_id += 1
            body: dict[str, Any] = {"device_id": device_id,
                                    "client_version": client_version}
            if access_token:
                body["access_token"] = access_token
            else:
                body.update({"login_id": login_id, "password": password,
                             "display_name": display_name})
                if register:
                    body["action"] = "register"
            self._send(FRAME_HANDSHAKE_REQ, request_id, body)
            frame_type, _, error, raw = self._read_frame()
            parsed = json.loads(raw.decode("utf-8")) if raw else {}
            if frame_type != FRAME_HANDSHAKE_RESP or error:
                raise TcpFrameError(parsed.get("reason") or parsed.get("message") or "handshake_failed")
            self.user_key = parsed.get("user_key", "")
            self.access_token = parsed.get("access_token", access_token)
            return parsed

    def request(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        with self._lock:
            request_id = self._next_id
            self._next_id += 1
            self._send(FRAME_REQUEST, request_id,
                       {"method": method, "params": params or {}})
            frame_type, _, error, raw = self._read_frame()
            parsed = json.loads(raw.decode("utf-8")) if raw else {}
            if frame_type == FRAME_ERROR or error:
                return {"ok": False, "error": parsed.get("reason") or "error",
                        "code": parsed.get("code", error), "message": parsed.get("message", "")}
            return {"ok": bool(parsed.get("ok")), "data": parsed.get("data", {}),
                    "code": 0, "message": ""}

    def ping(self) -> None:
        with self._lock:
            request_id = self._next_id
            self._next_id += 1
            self._send(FRAME_HEARTBEAT, request_id, {})
            self._read_frame()
