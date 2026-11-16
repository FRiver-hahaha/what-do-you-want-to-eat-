#!/usr/bin/env python3
"""P8-S3 Agent 真实调用契约验证：ServiceClient + diet_tools handler 全链路。"""

from __future__ import annotations

import os
import sys

ROOT = os.environ.get("P8_WORK_ROOT", "/home/m/eat-p8-s1")
sys.path.insert(0, ROOT)
os.environ["EAT_DISABLE_DOTENV"] = "1"

from agent.data.service_client import ServiceClient  # noqa: E402
from agent.tools import diet_tools  # noqa: E402
from agent.tools.base import ToolContext  # noqa: E402

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
FOOD_KEYS = {
    "id", "name", "category", "description", "calories", "protein", "fiber",
    "veggies", "water", "nature", "keywords", "meal_types", "price",
    "available", "image",
}

results: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, detail: object = "") -> bool:
    results.append((name, bool(cond), str(detail)))
    print(("PASS  " if cond else "FAIL  ") + name + ("" if cond else f" :: {detail}"))
    return bool(cond)


def main() -> int:
    client = ServiceClient(BASE, TOKEN)
    ctx = ToolContext(user_id="u_demo", session_id="s_s3_contract",
                      mode="work", service=client)

    check("ServiceClient 处于 service 模式", client.mode == "service", client.mode)

    lunch = client.get_menu("u_demo", "午餐")
    check("get_menu 午餐 source=service", lunch.get("source") == "service", lunch.get("source"))
    check("get_menu 午餐 count=14", lunch.get("count") == 14, lunch.get("count"))
    check("get_menu 午餐无 mock 回退异常", client.last_error == "", client.last_error)
    check("get_menu 菜品字段与 MOCK_FOODS 一致",
          all(set(item) >= FOOD_KEYS for item in lunch.get("foods") or []))
    lemon = next((f for f in lunch.get("foods") or [] if f.get("id") == "lemon-chicken"), {})
    check("get_menu 数值来自真实 foods 表",
          lemon.get("calories") == 460 and lemon.get("protein") == 38
          and lemon.get("veggies") == 320, lemon)
    check("get_menu meal_types 为数组、available 为 bool",
          isinstance(lemon.get("meal_types"), list) and isinstance(lemon.get("available"), bool),
          lemon)

    whole = client.get_menu("u_demo", "")
    check("get_menu 全量 count=16 且 meal_type=全部",
          whole.get("count") == 16 and whole.get("meal_type") == "全部", whole)

    snack = client.get_menu("u_demo", "加餐")
    check("get_menu 加餐 2 道",
          {f.get("id") for f in snack.get("foods") or []} == {"cheese-cake", "bubble-tea"},
          snack.get("count"))

    search_all = client.search_foods("u_demo", limit=16)
    check("search_foods 空条件返回 16 道且 source=service",
          search_all.get("count") == 16 and search_all.get("source") == "service",
          search_all.get("count"))

    chicken = client.search_foods("u_demo", query="鸡胸", limit=16)
    chicken_ids = {f.get("id") for f in chicken.get("foods") or []}
    check("search_foods q=鸡胸 命中鸡胸菜品",
          {"lemon-chicken", "chicken-salad"} <= chicken_ids, chicken_ids)

    limited = client.search_foods("u_demo", limit=3)
    check("search_foods limit=3 生效", limited.get("count") == 3, limited.get("count"))

    indulge = client.search_foods("u_demo", nature="indulge", limit=16)
    check("search_foods nature=indulge 返回 6 道", indulge.get("count") == 6,
          indulge.get("count"))

    empty = client.search_foods("u_demo", query="不存在的菜品xyz")
    check("search_foods 无结果 count=0", empty.get("count") == 0, empty)

    # 过敏过滤：通过 Agent 自己的 update_profile 写入，再由 search 验证。
    original = client.get_profile("u_demo")
    orig_allergies = original.get("allergies")
    try:
        client.update_profile("u_demo", {"allergies": ["虾"]})
        blocked = client.search_foods("u_demo", query="虾", limit=16)
        check("Agent 档案过敏=虾 后 search_foods 过滤虾类",
              blocked.get("count") == 0, blocked.get("foods"))
    finally:
        client.update_profile("u_demo", {"allergies": orig_allergies})

    restored = client.get_profile("u_demo")
    check("Agent 测试后过敏已恢复",
          restored.get("allergies") == orig_allergies, restored.get("allergies"))
    check("update_profile 全链路无 mock 回退", client.last_error == "", client.last_error)

    week = client.get_friend_ranking("u_demo", "week")
    week_ranking = week.get("ranking") or []
    check("get_friend_ranking week source=service",
          week.get("source") == "service", week.get("source"))
    check("get_friend_ranking week 仅含自己与 3 位正常好友",
          {item.get("user_id") for item in week_ranking} ==
          {"u_demo", "u_test_rank_a", "u_test_rank_b", "u_test_rank_c"},
          [item.get("user_id") for item in week_ranking])
    check("get_friend_ranking week 不含拉黑用户",
          "u_test_rank_d" not in {item.get("user_id") for item in week_ranking}, week_ranking)
    check("get_friend_ranking 每项字段齐全",
          all({"rank", "user_id", "name", "score"} <= set(item) for item in week_ranking),
          week_ranking)

    today = client.get_friend_ranking("u_demo", "today")
    check("get_friend_ranking today 返回 4 人",
          len(today.get("ranking") or []) == 4, today.get("ranking"))
    check("get_friend_ranking 全链路无 mock 回退", client.last_error == "", client.last_error)

    # diet_tools handler：验证 match 注入与 summary 文案。
    menu_result = diet_tools._menu({"meal_type": "午餐"}, ctx)
    menu_foods = (menu_result.data or {}).get("foods") or []
    check("diet_tools._menu 返回 14 道", menu_result.ok and menu_result.data["count"] == 14,
          menu_result.data.get("count") if menu_result.data else None)
    check("diet_tools._menu 为每道菜注入 match(20-98)",
          len(menu_foods) == 14 and
          all(isinstance(f.get("match"), int) and 20 <= f["match"] <= 98 for f in menu_foods),
          [f.get("match") for f in menu_foods])
    check("diet_tools._menu summary 使用真实条数", "14" in menu_result.summary,
          menu_result.summary)

    search_result = diet_tools._search_foods({"query": "鸡胸", "limit": 5}, ctx)
    search_foods = (search_result.data or {}).get("foods") or []
    check("diet_tools._search_foods 注入 match",
          search_result.ok and search_foods and
          all(isinstance(f.get("match"), int) for f in search_foods),
          search_result.data.get("count") if search_result.data else None)

    rank_result = diet_tools._friend_ranking({"scope": "week"}, ctx)
    check("diet_tools._friend_ranking 返回 4 人",
          rank_result.ok and len(rank_result.data["ranking"]) == 4,
          rank_result.data.get("ranking") if rank_result.data else None)
    check("diet_tools._friend_ranking summary 含人数与第一名",
          "共 4 人上榜" in rank_result.summary, rank_result.summary)

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())