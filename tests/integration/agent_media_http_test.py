"""Real HTTP check through the shared Agent for the cover/avatar flow.

Registers one throwaway account through the Agent, writes cover/avatar over the
Agent HTTP facade, reads them back, and proves that a body-supplied user_id
cannot redirect the write. The printed user key is meant to be deleted.

Run from the repository root with the Agent on 8787:
    PYTHONPATH=. python3 tests/integration/agent_media_http_test.py
"""

from __future__ import annotations

import argparse
import datetime
import json
import re
import urllib.request

AGENT = "http://127.0.0.1:8787"
PASSWORD = "RouteA!2026"


def call(path: str, payload: dict | None = None) -> dict:
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(AGENT + path, data=data, method="POST" if data else "GET")
    request.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.loads(response.read().decode("utf-8"))


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9443)
    args = parser.parse_args()

    captcha = call("/api/service/captcha", {"host": args.host, "port": args.port})
    captcha_id = str(captcha.get("captcha_id") or "")
    code = "".join(re.findall(r">([A-Z2-9])</text>", captcha.get("svg", "")))
    check(captcha_id and len(code) == 4, f"captcha unavailable: {captcha}")

    login_id = f"media_http_{int(datetime.datetime.now().timestamp() * 1000)}@test.local"
    login = call("/api/service/login", {
        "host": args.host, "port": args.port, "login_id": login_id,
        "password": PASSWORD, "register": True,
        "captcha_id": captcha_id, "captcha_code": code,
    })
    check(bool(login.get("ok")), f"agent login failed: {login}")
    user_key = str(login.get("user_key") or "")
    print(f"user_key={user_key}")

    cover = "data:image/jpeg;base64,/9j/HTTPCOVER"
    avatar = "data:image/png;base64,iVBORw0HTTPAVATAR"
    saved = call("/api/profile/cover", {"user_id": user_key, "cover": cover, "avatar": avatar})
    check(saved.get("ok") is not False, f"agent cover save failed: {saved}")
    check(saved.get("cover") == cover and saved.get("avatar") == avatar,
          f"agent cover save mismatch: {saved}")

    read_back = call(f"/api/profile/cover?user_id={user_key}")
    check(read_back.get("cover") == cover and read_back.get("avatar") == avatar,
          f"agent cover read mismatch: {read_back}")

    spoof = call("/api/profile/cover", {"user_id": "u_demo", "cover": "data:image/png;base64,SPOOF"})
    check(spoof.get("ok") is False and spoof.get("error") == "forbidden",
          f"cross-user write was not denied: {spoof}")

    after = call(f"/api/profile/cover?user_id={user_key}")
    check(after.get("cover") == cover, "denied write still changed the stored cover")

    call("/api/service/logout", {})
    print(json.dumps({"ok": True, "cleanup": [user_key], "login_id": login_id}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
