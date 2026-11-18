"""Real TCP check for the per-user ranking media store.

Registers three throwaway accounts (owner / peer / stranger), verifies that
cover and avatar writes land on the authenticated user only, that friends can
read each other, that non-friends are rejected, and that ranking exposes the
media version tokens. The printed user keys are meant to be deleted afterwards.

Run from the repository root:
    PYTHONPATH=. python3 tests/integration/profile_media_test.py --host 127.0.0.1 --port 9443
"""

from __future__ import annotations

import argparse
import datetime
import json
import re

from agent.transport.tcp_frame import TcpFrameClient

PASSWORD = "RouteA!2026"
FORBIDDEN = 2004


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def fetch_captcha(client: TcpFrameClient) -> tuple[str, str]:
    result = client.request("auth.captcha", {})
    check(bool(result.get("ok")), f"auth.captcha failed: {result}")
    data = result.get("data") or {}
    code = "".join(re.findall(r">([A-Z2-9])</text>", data.get("svg", "")))
    check(len(code) == 4, f"captcha svg parse failed: {data}")
    return str(data.get("captcha_id") or ""), code


def register(host: str, port: int, suffix: str) -> TcpFrameClient:
    client = TcpFrameClient(host, port, timeout=5.0)
    client.connect()
    login_id = f"media_{suffix}_{int(datetime.datetime.now().timestamp() * 1000)}@test.local"
    captcha_id, captcha_code = fetch_captcha(client)
    handshake = client.handshake(login_id=login_id, password=PASSWORD, register=True,
                                 display_name=f"Media {suffix}",
                                 captcha_id=captcha_id, captcha_code=captcha_code)
    check(bool(handshake.get("user_key")), "handshake returned no user_key")
    return client


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9443)
    args = parser.parse_args()

    owner = register(args.host, args.port, "owner")
    peer = register(args.host, args.port, "peer")
    stranger = register(args.host, args.port, "stranger")
    owner_key, peer_key = owner.user_key, peer.user_key
    print(f"owner={owner_key} peer={peer_key} stranger={stranger.user_key}")

    media = "data:image/jpeg;base64,/9j/4AAQSkZJRg=="
    written = owner.request("profile.media.set", {"idem_key": "media-owner-1",
                                                  "cover": media, "avatar": media})
    check(written["ok"], f"profile.media.set failed: {written}")
    check(written["data"]["cover_ver"] > 0, "cover_ver was not bumped")
    check(written["data"]["avatar_ver"] > 0, "avatar_ver was not bumped")

    own = owner.request("profile.media.get", {})
    check(own["ok"] and own["data"]["cover"] == media, f"own read mismatch: {own}")

    denied = stranger.request("profile.media.get", {"peer_key": owner_key})
    check(not denied["ok"] and int(denied.get("code") or 0) == FORBIDDEN,
          f"stranger was not rejected: {denied}")

    denied_write = stranger.request("profile.media.set", {"idem_key": "media-stranger-1",
                                                          "user_key": owner_key, "cover": media})
    check(denied_write["ok"], f"stranger set should only touch itself: {denied_write}")
    still = owner.request("profile.media.get", {})
    check(still["data"]["cover"] == media, "stranger overwrote another user's cover")

    requested = owner.request("friend.request", {"idem_key": "media-friend-req",
                                                "peer_key": peer_key})
    check(requested["ok"], f"friend.request failed: {requested}")
    accepted = peer.request("friend.accept", {"idem_key": "media-friend-acc",
                                             "peer_key": owner_key})
    check(accepted["ok"], f"friend.accept failed: {accepted}")

    friend_read = peer.request("profile.media.get", {"peer_key": owner_key})
    check(friend_read["ok"] and friend_read["data"]["cover"] == media,
          f"friend could not read cover: {friend_read}")

    forged = peer.request("profile.media.set", {"idem_key": "media-peer-1",
                                                "user_key": owner_key,
                                                "cover": "data:image/png;base64,AAAA"})
    check(forged["ok"], f"peer set failed: {forged}")
    owner_after = owner.request("profile.media.get", {})
    check(owner_after["data"]["cover"] == media, "forged user_key rewrote the owner row")
    peer_after = peer.request("profile.media.get", {})
    check(peer_after["data"]["cover"] == "data:image/png;base64,AAAA",
          f"peer write did not land on the peer row: {peer_after}")

    ranking = owner.request("ranking.friends", {"scope": "week"})
    check(ranking["ok"], f"ranking.friends failed: {ranking}")
    row = next((item for item in ranking["data"]["ranking"]
                if item["user_id"] == peer_key), None)
    check(row is not None, "ranking omitted the peer")
    check(int(row.get("cover_ver") or 0) > 0, f"ranking did not expose cover_ver: {row}")

    owner.close()
    peer.close()
    stranger.close()
    print(json.dumps({"ok": True, "cleanup": [owner_key, peer_key, stranger.user_key]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
