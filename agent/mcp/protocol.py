from __future__ import annotations

import json
from typing import Any

JSONRPC_VERSION = "2.0"
MCP_PROTOCOL_VERSION = "2024-11-05"

PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602
INTERNAL_ERROR = -32603
TOOL_NOT_FOUND = -32001
TOOL_EXECUTION_ERROR = -32002


class MCPProtocolError(RuntimeError):
    pass


def request_message(request_id: int | str, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    message: dict[str, Any] = {"jsonrpc": JSONRPC_VERSION, "id": request_id, "method": method}
    if params is not None:
        message["params"] = params
    return message


def notification_message(method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    message: dict[str, Any] = {"jsonrpc": JSONRPC_VERSION, "method": method}
    if params is not None:
        message["params"] = params
    return message


def response_message(request_id: int | str, result: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": JSONRPC_VERSION, "id": request_id, "result": result}


def error_message(request_id: int | str | None, code: int, message: str, data: Any = None) -> dict[str, Any]:
    error: dict[str, Any] = {"code": code, "message": message}
    if data is not None:
        error["data"] = data
    return {"jsonrpc": JSONRPC_VERSION, "id": request_id, "error": error}


def encode_message(message: dict[str, Any]) -> str:
    return json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n"


def decode_message(line: str) -> dict[str, Any]:
    stripped = line.strip()
    if not stripped:
        raise MCPProtocolError("空消息")
    try:
        message = json.loads(stripped)
    except json.JSONDecodeError as exc:
        raise MCPProtocolError(f"JSON 解析失败：{exc}") from exc
    if not isinstance(message, dict):
        raise MCPProtocolError("消息必须是 JSON 对象")
    if message.get("jsonrpc") != JSONRPC_VERSION:
        raise MCPProtocolError("不支持的 JSON-RPC 版本")
    return message


def tool_result(text: str, structured: Any = None, is_error: bool = False) -> dict[str, Any]:
    result: dict[str, Any] = {"content": [{"type": "text", "text": text}], "isError": is_error}
    if structured is not None:
        result["structuredContent"] = structured
    return result