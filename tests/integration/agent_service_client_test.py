"""Verify the Agent ServiceClient now talks to the C++ server over TCP frames."""

from __future__ import annotations

import datetime
import re
import sys

from agent.data.service_client import ServiceClient
from agent.transport.tcp_frame import TcpFrameClient

PASSWORD = "RouteA!2026"


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def fetch_captcha(client: TcpFrameClient) -> tuple[str, str]:
    result = client.request("auth.captcha", {})
    check(result.get("ok"), f"auth.captcha failed: {result}")
    data = result.get("data") or {}
    code = "".join(re.findall(r">([A-Z2-9])</text>", data.get("svg", "")))
    check(len(code) == 4, f"captcha svg parse failed: {data}")
    return str(data.get("captcha_id") or ""), code


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 19443

    login_id = f"agenttcp_{int(datetime.datetime.now().timestamp() * 1000)}@test.local"
    bootstrap = TcpFrameClient(host, port, timeout=5.0)
    bootstrap.connect()
    captcha_id, captcha_code = fetch_captcha(bootstrap)
    handshake = bootstrap.handshake(login_id=login_id, password=PASSWORD, register=True,
                                    display_name="Agent TCP",
                                    captcha_id=captcha_id, captcha_code=captcha_code)
    token = str(handshake.get("access_token") or "")
    user_key = str(handshake.get("user_key") or "")
    check(bool(token) and bool(user_key), "handshake did not return a session")

    client = ServiceClient(tcp_host=host, tcp_port=port, token=token, user_key=user_key)
    check(client.mode == "tcp", f"unexpected mode {client.mode}")
    check(client.strict, "configured service must be strict")

    profile = client.get_profile(user_key)
    check(profile.get("user_id") == user_key, f"profile user mismatch: {profile}")
    check(profile.get("source") == "service", f"profile source: {profile.get('source')}")

    updated = client.update_profile(user_key, {"activity": "适中", "water_target": 2100,
                                               "name": "Agent 测试"})
    check(updated.get("ok"), f"update_profile failed: {updated}")
    check(updated.get("profile", {}).get("water_target") == 2100, f"profile not updated: {updated}")

    today = datetime.date.today().isoformat()
    recorded = client.record_meal(user_key, {
        "date": "", "type": "早餐", "text": "豆浆、全麦面包",
        "items": ["豆浆", "全麦面包"], "protein": 15, "fiber": 4,
        "veggies": 0, "water": 250,
    }, "agent-meal-1")
    check(recorded.get("ok") is not False, f"record_meal failed: {recorded}")
    check(recorded.get("receipt_id", "").startswith("meal-"), f"receipt: {recorded}")
    check(recorded.get("meal", {}).get("date") == today, f"meal date: {recorded}")

    duplicate = client.record_meal(user_key, {
        "date": "", "type": "早餐", "text": "豆浆、全麦面包",
        "items": ["豆浆", "全麦面包"], "protein": 15, "fiber": 4,
        "veggies": 0, "water": 250,
    }, "agent-meal-1")
    check(duplicate.get("idempotent") is True, f"idempotent replay failed: {duplicate}")

    recent = client.get_recent_meals(user_key, 2)
    check(recent.get("count") == 1, f"recent meals: {recent}")
    check(recent.get("source") == "service", "recent meals source")

    gap = client.get_nutrition_gap(user_key, today)
    check(gap.get("gap", {}).get("water") == max(0, 2100 - 250), f"gap: {gap}")
    check(gap.get("targets", {}).get("water") == 2100, f"water target: {gap}")

    weekly = client.get_weekly_stats(user_key)
    check(weekly.get("days_recorded") >= 1, f"weekly: {weekly}")

    foods = client.search_foods(user_key, query="鸡", limit=5)
    check(foods.get("count", 0) > 0, f"search: {foods}")

    menu = client.get_menu(user_key, "早餐")
    check(menu.get("count", 0) > 0, f"menu: {menu}")

    friends = client.get_friends(user_key)
    check("friends" in friends, f"friends: {friends}")

    ranking = client.get_friend_ranking(user_key, "week")
    check(any(row.get("user_id") == user_key for row in ranking.get("ranking", [])),
          f"ranking: {ranking}")

    broken = ServiceClient(tcp_host=host, tcp_port=1, token=token, user_key=user_key)
    failure = broken.get_profile(user_key)
    check(failure.get("source") == "service_error",
          f"strict mode must not fall back to mock: {failure}")
    check(failure.get("ok") is False, f"strict failure shape: {failure}")

    bootstrap.close()
    print("agent_service_client_ok", user_key, recorded.get("meal", {}).get("id"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
