"""Real TCP end-to-end check for the consolidated business methods.

Run from the repository root:
    python3 tests/integration/service_tcp_test.py --host 127.0.0.1 --port 19443
"""

from __future__ import annotations

import argparse
import datetime
import json
import sys
from typing import Any

from agent.transport.tcp_frame import TcpFrameClient

PASSWORD = "RouteA!2026"


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def register(host: str, port: int, suffix: str) -> TcpFrameClient:
    client = TcpFrameClient(host, port, timeout=5.0)
    client.connect()
    login_id = f"routea_{suffix}_{int(datetime.datetime.now().timestamp() * 1000)}@test.local"
    handshake = client.handshake(login_id=login_id, password=PASSWORD, register=True,
                                 display_name=f"RouteA {suffix}")
    check(bool(handshake.get("user_key")), "handshake returned no user_key")
    return client


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19443)
    args = parser.parse_args()

    owner = register(args.host, args.port, "owner")
    peer = register(args.host, args.port, "peer")
    owner_key = owner.user_key
    peer_key = peer.user_key
    today = datetime.date.today().isoformat()
    print(f"owner={owner_key} peer={peer_key}")

    profile = owner.request("profile.get", {})
    check(profile["ok"], f"profile.get failed: {profile}")
    check(profile["data"]["user_id"] == owner_key, "profile user_id mismatch")

    patched = owner.request("profile.patch", {
        "idem_key": "profile-patch-1", "name": "测试用户", "activity": "适中",
        "water_target": 2000,
    })
    check(patched["ok"], f"profile.patch failed: {patched}")
    check(patched["data"]["name"] == "测试用户", "profile patch not persisted")

    meal_body = {
        "idem_key": "meal-1", "date": today, "type": "早餐",
        "text": "燕麦酸奶、蓝莓、水煮蛋", "items": ["燕麦酸奶", "蓝莓", "水煮蛋"],
        "protein": 22, "fiber": 6, "veggies": 0, "water": 300,
    }
    created = owner.request("meal.create", meal_body)
    check(created["ok"], f"meal.create failed: {created}")
    meal_id = created["data"]["meal"]["id"]
    check(created["data"]["idempotent"] is False, "first create marked idempotent")
    print("meal.created", meal_id)

    replayed = owner.request("meal.create", meal_body)
    check(replayed["ok"], f"meal.create replay failed: {replayed}")
    check(replayed["data"]["meal"]["id"] == meal_id, "replay returned a different meal")
    check(replayed["data"]["idempotent"] is True, "replay not marked idempotent")

    conflicting = owner.request("meal.create", dict(meal_body, text="完全不同的内容"))
    check(not conflicting["ok"], "conflicting idem_key accepted")
    check(conflicting["code"] == 3003, f"expected 3003 conflict, got {conflicting}")

    recent = owner.request("meal.recent", {"days": 2})
    check(recent["ok"], f"meal.recent failed: {recent}")
    check(recent["data"]["count"] == 1, f"unexpected meal count {recent['data']['count']}")

    gap = owner.request("stats.nutrition_gap", {"date": today})
    check(gap["ok"], f"stats.nutrition_gap failed: {gap}")
    targets = gap["data"]["targets"]
    intake = gap["data"]["intake"]
    deficit = gap["data"]["gap"]
    for key in ("protein", "fiber", "veggies", "water"):
        expected = max(0.0, float(targets[key]) - float(intake[key]))
        check(abs(float(deficit[key]) - expected) < 1e-6,
              f"gap identity broken for {key}: {deficit[key]} != {expected}")
    check(intake["water"] == 300, f"unexpected water intake {intake['water']}")

    weekly = owner.request("stats.weekly", {})
    check(weekly["ok"], f"stats.weekly failed: {weekly}")
    check(weekly["data"]["days_recorded"] >= 1, "weekly did not count today")

    updated = owner.request("meal.patch", {
        "idem_key": "meal-patch-1", "id": meal_id, "text": "奶茶配炸鸡",
        "protein": 10, "water": 100,
    })
    check(updated["ok"], f"meal.patch failed: {updated}")
    check(updated["data"]["meal"]["version"] == 2, "patch did not bump version")
    check(updated["data"]["meal"]["penalty"] == 7, "penalty was not recomputed")

    gap_after = owner.request("stats.nutrition_gap", {"date": today})
    check(gap_after["ok"], f"gap after patch failed: {gap_after}")
    check(gap_after["data"]["intake"]["water"] == 100, "patch did not affect stats")

    stale = owner.request("meal.patch", {
        "idem_key": "meal-patch-stale", "id": meal_id, "version": 1, "text": "过期更新",
    })
    check(not stale["ok"] and stale["code"] == 3005,
          f"expected version conflict 3005, got {stale}")

    menu = owner.request("food.menu", {"meal_type": "早餐"})
    check(menu["ok"] and menu["data"]["count"] > 0, f"food.menu failed: {menu}")
    check(all("早餐" in item["meal_types"] for item in menu["data"]["foods"]),
          "menu returned a non-breakfast dish")

    search = owner.request("food.search", {"query": "鸡", "limit": 5})
    check(search["ok"] and search["data"]["count"] > 0, f"food.search failed: {search}")

    requested = owner.request("friend.request", {"idem_key": "friend-req-1",
                                                "peer_key": peer_key})
    check(requested["ok"], f"friend.request failed: {requested}")
    check(requested["data"]["status"] == "pending", "friend request not pending")

    accepted = peer.request("friend.accept", {"idem_key": "friend-acc-1",
                                             "peer_key": owner_key})
    check(accepted["ok"] and accepted["data"]["status"] == "friend",
          f"friend.accept failed: {accepted}")

    owner_friends = owner.request("friend.list", {})
    peer_friends = peer.request("friend.list", {})
    check(owner_friends["ok"] and peer_friends["ok"], "friend.list failed")
    check(any(item["id"] == peer_key for item in owner_friends["data"]["friends"]),
          "owner does not see peer")
    check(any(item["id"] == owner_key for item in peer_friends["data"]["friends"]),
          "peer does not see owner")

    ranking = owner.request("ranking.friends", {"scope": "week"})
    check(ranking["ok"], f"ranking.friends failed: {ranking}")
    rows = ranking["data"]["ranking"]
    check(any(row["user_id"] == owner_key for row in rows), "ranking omits self")
    check(any(row["user_id"] == peer_key for row in rows), "ranking omits friend")
    check([row["rank"] for row in rows] == list(range(1, len(rows) + 1)),
          f"ranking is not contiguous: {rows}")

    deleted = owner.request("meal.delete", {"idem_key": "meal-del-1", "id": meal_id})
    check(deleted["ok"], f"meal.delete failed: {deleted}")
    gap_zero = owner.request("stats.nutrition_gap", {"date": today})
    check(gap_zero["ok"] and gap_zero["data"]["intake"]["water"] == 0,
          f"delete did not clear stats: {gap_zero}")

    removed = owner.request("friend.remove", {"idem_key": "friend-rm-1", "peer_key": peer_key})
    check(removed["ok"], f"friend.remove failed: {removed}")
    owner_friends = owner.request("friend.list", {})
    check(not owner_friends["data"]["friends"], "friend.remove left the relation behind")

    owner.close()
    peer.close()
    print(json.dumps({"ok": True, "user_key": owner_key, "meal_id": meal_id}))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001 - test script reports the failure
        print(json.dumps({"ok": False, "error": str(exc)}), file=sys.stderr)
        raise
