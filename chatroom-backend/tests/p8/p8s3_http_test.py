#!/usr/bin/env python3
"""P8-S3 HTTP 契约与真实 DB 行为测试（菜单 / 菜品搜索 / 好友排行榜）。"""

from __future__ import annotations

import json
import sys
import urllib.error
import urllib.parse
import urllib.request

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))

ALL_FOOD_IDS = {
    "lemon-chicken", "tomato-beef", "mushroom-soup", "shrimp-pasta",
    "vegetable-rice", "pepper-chicken", "tomato-egg-rice", "salmon-quinoa",
    "steamed-fish-rice", "chicken-salad", "yangzhou-fried-rice",
    "beef-fried-rice", "fried-chicken-burger", "cheese-cake", "bubble-tea",
    "malatang",
}
INDULGE_IDS = {
    "yangzhou-fried-rice", "beef-fried-rice", "fried-chicken-burger",
    "cheese-cake", "bubble-tea", "malatang",
}
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


def call(method: str, path: str, body: dict | None = None) -> tuple[int, dict]:
    data = None
    if body is not None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(BASE + path, data=data, method=method)
    req.add_header("Authorization", f"Bearer {TOKEN}")
    req.add_header("Content-Type", "application/json; charset=utf-8")
    try:
        with OPENER.open(req, timeout=5) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return exc.code, {"raw": exc.read().decode("utf-8", "replace")}


def q(value: str) -> str:
    return urllib.parse.quote(value)


def ids(payload: dict) -> set[str]:
    return {item.get("id") for item in payload.get("foods") or []}


def main() -> int:
    # ---------- 健康检查 ----------
    status, health = call("GET", "/health")
    check("health 200 且 version=p8-s3/p8-s4",
          status == 200 and health.get("version") in {"p8-s3", "p8-s4"}, health)

    # ---------- 菜单 ----------
    status, menu = call("GET", "/api/v1/menu")
    check("menu 默认 200 + ok", status == 200 and menu.get("ok") is True, menu)
    check("menu 默认 meal_type=全部", menu.get("meal_type") == "全部", menu.get("meal_type"))
    check("menu 默认返回 16 道在售菜品",
          menu.get("count") == 16 and len(menu.get("foods") or []) == 16, menu.get("count"))
    check("menu 菜品 id 与 foods 表完整一致", ids(menu) == ALL_FOOD_IDS,
          sorted(ALL_FOOD_IDS - ids(menu)) + sorted(ids(menu) - ALL_FOOD_IDS))
    check("menu source=service", menu.get("source") == "service", menu.get("source"))
    check("menu 每条菜品字段齐全", all(set(item) >= FOOD_KEYS for item in menu.get("foods") or []))
    check("menu meal_types 为数组且 available 为布尔",
          all(isinstance(item.get("meal_types"), list) and isinstance(item.get("available"), bool)
              for item in menu.get("foods") or []))
    check("menu 数值字段为数字",
          all(isinstance(item.get(k), (int, float))
              for item in menu.get("foods") or []
              for k in ("calories", "protein", "fiber", "veggies", "water", "price")))

    status, breakfast = call("GET", "/api/v1/menu?meal_type=" + q("早餐"))
    check("menu 早餐 3 道", breakfast.get("count") == 3, ids(breakfast))
    check("menu 早餐 meal_type 回显", breakfast.get("meal_type") == "早餐", breakfast.get("meal_type"))

    status, lunch = call("GET", "/api/v1/menu?meal_type=" + q("午餐"))
    check("menu 午餐 14 道", lunch.get("count") == 14, lunch.get("count"))

    status, snack = call("GET", "/api/v1/menu?meal_type=" + q("加餐"))
    check("menu 加餐 2 道", ids(snack) == {"cheese-cake", "bubble-tea"}, ids(snack))

    status, bad_meal = call("GET", "/api/v1/menu?meal_type=" + q("夜宵"))
    check("menu 非法 meal_type 返回 invalid_params",
          status == 200 and bad_meal.get("error") == "invalid_params", bad_meal)

    status, bad_uid = call("GET", "/api/v1/menu?user_id=bad%20uid")
    check("menu 非法 user_id 形状返回 invalid_params",
          status == 200 and bad_uid.get("error") == "invalid_params", bad_uid)

    status, forbidden_uid = call("GET", "/api/v1/menu?user_id=u_lin")
    check("menu 非白名单 user_id 返回 forbidden",
          status == 200 and forbidden_uid.get("error") == "forbidden", forbidden_uid)

    # ---------- 菜品搜索 ----------
    status, all_foods = call("GET", "/api/v1/foods/search?limit=16")
    check("search 空条件 limit=16 返回全部 16 道",
          all_foods.get("count") == 16, all_foods.get("count"))
    check("search source=service", all_foods.get("source") == "service", all_foods.get("source"))

    status, chicken = call("GET", "/api/v1/foods/search?q=" + q("鸡胸") + "&limit=16")
    check("search q=鸡胸 命中鸡胸相关菜品",
          {"lemon-chicken", "chicken-salad"} <= ids(chicken), ids(chicken))
    check("search q=鸡胸 不含无关菜品", "cheese-cake" not in ids(chicken), ids(chicken))

    status, none = call("GET", "/api/v1/foods/search?q=" + q("不存在的菜品xyz"))
    check("search 无结果返回 200/ok/count=0/空数组",
          status == 200 and none.get("ok") is True
          and none.get("count") == 0 and none.get("foods") == [], none)

    status, limited = call("GET", "/api/v1/foods/search?limit=3")
    check("search limit=3 截断", limited.get("count") == 3, limited.get("count"))

    status, default_limit = call("GET", "/api/v1/foods/search?limit=0")
    check("search limit=0 使用默认 5", default_limit.get("count") == 5,
          default_limit.get("count"))

    status, over_limit = call("GET", "/api/v1/foods/search?limit=17")
    check("search limit=17 返回 invalid_params",
          status == 200 and over_limit.get("error") == "invalid_params", over_limit)

    status, text_limit = call("GET", "/api/v1/foods/search?limit=abc")
    check("search limit=abc 返回 invalid_params",
          status == 200 and text_limit.get("error") == "invalid_params", text_limit)

    status, indulge = call("GET", "/api/v1/foods/search?nature=indulge&limit=16")
    check("search nature=indulge 返回 6 道放纵菜",
          ids(indulge) == INDULGE_IDS, ids(indulge))

    status, recommend = call("GET", "/api/v1/foods/search?nature=recommend&limit=16")
    check("search nature=recommend 返回 10 道推荐菜",
          recommend.get("count") == 10, recommend.get("count"))

    status, bad_nature = call("GET", "/api/v1/foods/search?nature=unhealthy")
    check("search 非法 nature 返回 invalid_params",
          status == 200 and bad_nature.get("error") == "invalid_params", bad_nature)

    status, by_meal = call("GET", "/api/v1/foods/search?meal_type=" + q("加餐") + "&limit=16")
    check("search meal_type=加餐 返回 2 道", ids(by_meal) == {"cheese-cake", "bubble-tea"}, ids(by_meal))

    status, tagged = call("GET", "/api/v1/foods/search?tags=" + q("减脂") + "&limit=16")
    check("search tags=减脂 命中鸡胸蔬菜沙拉",
          ids(tagged) == {"chicken-salad"}, ids(tagged))

    status, multi_tag = call("GET", "/api/v1/foods/search?tags=" + q("减脂,清淡") + "&limit=16")
    check("search tags 逗号分隔取并集",
          {"chicken-salad", "mushroom-soup"} <= ids(multi_tag), ids(multi_tag))

    status, bad_tag = call("GET", "/api/v1/foods/search?tags=" + q("x" * 31))
    check("search tag 超长返回 invalid_params",
          status == 200 and bad_tag.get("error") == "invalid_params", bad_tag)

    # ---------- 过敏 / 忌口过滤（真实档案） ----------
    status, original = call("GET", "/api/v1/users/u_demo/health-profile")
    orig_allergies = original.get("allergies")
    orig_dislikes = original.get("dislikes")
    check("读取 u_demo 原始过敏/忌口成功",
          isinstance(orig_allergies, list) and isinstance(orig_dislikes, list), original)

    try:
        call("PATCH", "/api/v1/users/u_demo/health-profile", {"allergies": ["虾"]})
        status, shrimp = call("GET", "/api/v1/foods/search?q=" + q("虾") + "&limit=16")
        check("过敏=虾 时搜索虾类返回 0 道",
              shrimp.get("count") == 0, ids(shrimp))

        call("PATCH", "/api/v1/users/u_demo/health-profile", {"dislikes": ["鸡胸"]})
        status, dislike = call("GET", "/api/v1/foods/search?q=" + q("鸡胸") + "&limit=16")
        check("忌口=鸡胸 时搜索鸡胸返回 0 道",
              dislike.get("count") == 0, ids(dislike))
    finally:
        call("PATCH", "/api/v1/users/u_demo/health-profile",
             {"allergies": orig_allergies, "dislikes": orig_dislikes})

    status, restored = call("GET", "/api/v1/users/u_demo/health-profile")
    check("测试后过敏/忌口已恢复",
          restored.get("allergies") == orig_allergies
          and restored.get("dislikes") == orig_dislikes, restored)

    status, shrimp_back = call("GET", "/api/v1/foods/search?q=" + q("虾") + "&limit=16")
    check("恢复后搜索虾重新命中虾仁意面",
          "shrimp-pasta" in ids(shrimp_back), ids(shrimp_back))

    # ---------- 好友排行榜 ----------
    status, week = call("GET", "/api/v1/users/u_demo/ranking?scope=week")
    check("ranking 200 + ok + scope=week",
          status == 200 and week.get("ok") is True and week.get("scope") == "week", week)
    check("ranking source=service", week.get("source") == "service", week.get("source"))
    ranking = week.get("ranking") or []
    ranking_ids = [item.get("user_id") for item in ranking]
    check("ranking 仅含自己 + 3 位正常好友",
          set(ranking_ids) == {"u_demo", "u_test_rank_a", "u_test_rank_b", "u_test_rank_c"},
          ranking_ids)
    check("ranking 不含拉黑/待验证用户 u_test_rank_d",
          "u_test_rank_d" not in ranking_ids, ranking_ids)
    check("ranking 每项字段齐全",
          all({"rank", "user_id", "name", "score"} <= set(item) for item in ranking), ranking)

    by_uid = {item["user_id"]: item for item in ranking}
    check("ranking 自己展示名为「我」", by_uid.get("u_demo", {}).get("name") == "我",
          by_uid.get("u_demo"))
    check("ranking 好友名优先取 health_profile.name",
          by_uid.get("u_test_rank_a", {}).get("name") == "甲同学", by_uid.get("u_test_rank_a"))
    check("ranking 无档案好友回退 user.name",
          by_uid.get("u_test_rank_c", {}).get("name") == "测试丙", by_uid.get("u_test_rank_c"))
    check("ranking 无饮食记录好友周分为 0",
          by_uid.get("u_test_rank_a", {}).get("score") == 0
          and by_uid.get("u_test_rank_b", {}).get("score") == 0, ranking)
    check("ranking 并列分数共享同一名次",
          by_uid.get("u_test_rank_a", {}).get("rank") == by_uid.get("u_test_rank_b", {}).get("rank")
          and by_uid.get("u_test_rank_c", {}).get("rank") == by_uid.get("u_test_rank_a", {}).get("rank"),
          ranking)
    scores = [item["score"] for item in ranking]
    check("ranking 按分数降序", scores == sorted(scores, reverse=True), scores)
    check("ranking 首项名次为 1", ranking and ranking[0]["rank"] == 1, ranking[:1])

    status, today = call("GET", "/api/v1/users/u_demo/ranking?scope=today")
    today_ranking = today.get("ranking") or []
    today_by_uid = {item["user_id"]: item for item in today_ranking}
    check("ranking today 返回同一好友集合",
          set(today_by_uid) == {"u_demo", "u_test_rank_a", "u_test_rank_b", "u_test_rank_c"},
          list(today_by_uid))
    check("ranking today 无记录好友按活动量得基线 71 分",
          today_by_uid.get("u_test_rank_a", {}).get("score") == 71
          and today_by_uid.get("u_test_rank_b", {}).get("score") == 71, today_ranking)
    check("ranking today 无档案好友基线 68 分",
          today_by_uid.get("u_test_rank_c", {}).get("score") == 68, today_ranking)
    check("ranking today 分数在 45-96 之间",
          all(45 <= item["score"] <= 96 for item in today_ranking), today_ranking)

    status, bad_scope = call("GET", "/api/v1/users/u_demo/ranking?scope=month")
    check("ranking 非法 scope 返回 invalid_params",
          status == 200 and bad_scope.get("error") == "invalid_params", bad_scope)

    status, missing_user = call("GET", "/api/v1/users/u_nobody/ranking")
    # p4_demo 单用户模式下，非白名单 UID 在 Router 层就被拒绝，
    # 不会进入 Service 的 user_not_found 分支（该分支为防御性代码）。
    check("ranking 非白名单用户返回 forbidden",
          status == 200 and missing_user.get("error") == "forbidden", missing_user)

    status, bad_rank_uid = call("GET", "/api/v1/users/bad%20uid/ranking")
    check("ranking 非法 UID 形状返回 invalid_params",
          status == 200 and bad_rank_uid.get("error") == "invalid_params", bad_rank_uid)

    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    print(f"\nRESULT {passed}/{total} PASS")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())