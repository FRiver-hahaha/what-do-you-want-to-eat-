from __future__ import annotations

import argparse
import http.client
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def request(port: int, method: str, path: str, payload: dict | None = None) -> tuple[int, str]:
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
    try:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8") if payload is not None else None
        headers = {"Content-Type": "application/json"} if body is not None else {}
        connection.request(method, path, body=body, headers=headers)
        response = connection.getresponse()
        return response.status, response.read().decode("utf-8")
    finally:
        connection.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Agent 服务真实进程验收")
    parser.add_argument("--port", type=int, default=8787)
    args = parser.parse_args()

    process = subprocess.Popen(
        [sys.executable, "-m", "agent", "--host", "127.0.0.1", "--port", str(args.port)],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        ready = False
        for _ in range(80):
            if process.poll() is not None:
                break
            try:
                status, _ = request(args.port, "GET", "/health")
                if status == 200:
                    ready = True
                    break
            except OSError:
                time.sleep(0.25)
        if not ready:
            print("服务未就绪")
            return 1

        status, body = request(args.port, "GET", "/api/agent/meta")
        meta = json.loads(body)
        print(f"meta status={status} modes={[mode['id'] for mode in meta['modes']]}")
        print(f"models={[model['id'] for model in meta['models']]}")
        print(f"mcp={meta['mcp']['servers']}")
        print(f"rag={meta['rag']}")

        for message, mode in (("今天吃了什么", "qa"), ("今天该吃什么", "qa"), ("帮我看看实时菜单有什么", "work")):
            status, body = request(
                args.port,
                "POST",
                "/api/chat",
                {"user_id": "u_demo", "session_id": f"live_{mode}", "message": message, "mode": mode},
            )
            payload = json.loads(body)
            tools = [event["data"].get("name") for event in payload["events"] if event["type"] == "tool_call"]
            print(f"[{mode}] {message} -> status={status} tools={tools}")
            print(payload["answer"][:160].replace("\n", " "))

        status, body = request(
            args.port,
            "POST",
            "/api/skills/market/share",
            {"id": "nutritionist", "from_user": "u_demo", "to_users": ["u_stranger"]},
        )
        print(f"share stranger -> status={status} body={body[:120]}")
        return 0
    finally:
        process.terminate()
        try:
            output = process.communicate(timeout=8)[0]
        except subprocess.TimeoutExpired:
            process.kill()
            output = process.communicate()[0]
        print("--- server log ---")
        print((output or "")[-1200:])


if __name__ == "__main__":
    raise SystemExit(main())