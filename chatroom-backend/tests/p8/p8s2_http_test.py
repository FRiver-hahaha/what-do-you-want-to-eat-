import json
import sys
import threading
import time
import urllib.error
import urllib.request
from datetime import date, timedelta

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
UID = "u_demo"
MEALS = f"/api/v1/users/{UID}/meals"
GAP = f"/api/v1/users/{UID}/nutrition-gap"
WEEK = f"/api/v1/users/{UID}/weekly-stats"
PROFILE = f"/api/v1/users/{UID}/health-profile"

results = []
_seq = [0]
_seq_lock = threading.Lock()


def check(name, cond, detail=""):
    results.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else " :: " + str(detail)))


def next_key(prefix):
    with _seq_lock:
        _seq[0] += 1
        n = _seq[0]
    return f"{prefix}-{int(time.time() * 1000)}-{n}"


def call(method, path, body=None, token=TOKEN, timeout=20):
    data = None if body is None else json.dumps(body, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(BASE + path, data=data, method=method)
    req.add_header("Content-Type", "application/json; charset=utf-8")
    if token is not None:
        req.add_header("Authorization", "Bearer " + token)
    try:
        with OPENER.open(req, timeout=timeout) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return exc.code, json.loads(exc.read().decode("utf-8") or "{}")


def today():
    return date.today().isoformat()


def week_start():
    d = date.today()
    return (d - timedelta(days=d.weekday())).isoformat()


def meals_all(days=90):
    status, body = call("GET", f"{MEALS}?days={days}")
    return status, body


def gap_of(day):
    return call("GET", f"{GAP}?date={day}")


def week_of():
    return call("GET", WEEK)


def today_daily(body):
    for item in body.get("daily") or []:
        if item.get("date") == today():
            return item
    return None


def expected_score(intake, meals_count, penalty, bonus):
    ratio = lambda v, t: min(float(v) / float(t), 1.0)
    raw = (68.0 + ratio(intake["veggies"], 500) * 8 + ratio(intake["protein"], 90) * 6
           + ratio(intake["fiber"], 25) * 5 + min(meals_count, 3) + bonus - penalty)
    score = int(round(raw))
    return max(45, min(96, score))


# ── 0. 健康检查 ──
status, health = call("GET", "/health", token=None)
check("health 200 ok", status == 200 and health.get("ok") is True, (status, health))
check("health version p8-s2/p8-s3/p8-s4", health.get("version") in {"p8-s2", "p8-s3", "p8-s4"}, health)

# ── 1. 认证与白名单 ──
status, body = call("POST", MEALS, {"idempotency_key": next_key("k"), "text": "x"}, token=None)
check("POST meals no token -> unauthorized", status == 200 and body.get("error") == "unauthorized", body)
status, body = call("GET", MEALS, token=None)
check("GET meals no token -> unauthorized", body.get("error") == "unauthorized", body)
status, body = call("GET", GAP, token=None)
check("GET gap no token -> unauthorized", body.get("error") == "unauthorized", body)
status, body = call("GET", WEEK, token=None)
check("GET weekly no token -> unauthorized", body.get("error") == "unauthorized", body)
status, body = call("GET", "/api/v1/users/u_other/meals")
check("other uid meals -> forbidden", body.get("error") == "forbidden", body)
status, body = call("GET", "/api/v1/users/u.demo/meals")
check("invalid uid meals -> invalid_params", body.get("error") == "invalid_params", body)

# ── 2. 参数校验 ──
status, body = call("POST", MEALS, {"text": "no key"})
check("missing idempotency_key -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("POST", MEALS, {"idempotency_key": "bad key!", "text": "x"})
check("illegal idempotency_key chars -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("POST", MEALS, {"idempotency_key": next_key("k"), "date": "2026-13-01", "text": "x"})
check("invalid date -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("POST", MEALS, {"idempotency_key": next_key("k"), "date": "1999-01-01", "text": "x"})
check("out-of-range date -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("POST", MEALS, {"idempotency_key": next_key("k"), "items": "oops"})
check("items non-array -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("POST", MEALS, {"idempotency_key": next_key("k"), "items": [1, 2]})
check("items non-string -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("POST", MEALS, {"idempotency_key": next_key("k"), "protein": -1})
check("negative protein -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("POST", MEALS, {"idempotency_key": next_key("k"), "protein": 10001})
check("protein too large -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("POST", MEALS, {"idempotency_key": next_key("k"), "type": 5})
check("type non-string -> invalid_params", body.get("error") == "invalid_params", body)

status, body = call("GET", f"{MEALS}?days=0")
check("days=0 -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("GET", f"{MEALS}?days=abc")
check("days=abc -> invalid_params", body.get("error") == "invalid_params", body)
check("days=91 -> invalid_params", call("GET", f"{MEALS}?days=91")[1].get("error") == "invalid_params")
check("gap invalid date -> invalid_params", call("GET", f"{GAP}?date=2026-02-30")[1].get("error") == "invalid_params")

# ── 3. 首次写入：真实结构与缺省值 ──
key_default = next_key("default")
status, before = meals_all()
count_before = before.get("count", -1)
status, body = call("POST", MEALS, {"idempotency_key": key_default, "items": ["燕麦酸奶", "水煮蛋"]})
ok = status == 200 and body.get("ok") is True
check("record meal ok", ok, body)
meal = body.get("meal") or {}
check("receipt_id format", str(body.get("receipt_id", "")).startswith("meal-"), body.get("receipt_id"))
check("record idempotent=false", body.get("idempotent") is False, body)
check("record source=service", body.get("source") == "service", body)
check("record user_id", body.get("user_id") == UID, body)
check("default date == today", meal.get("date") == today(), meal.get("date"))
check("default type == 加餐", meal.get("type") == "加餐", meal.get("type"))
check("default text joins items", meal.get("text") == "燕麦酸奶、水煮蛋", meal.get("text"))
check("items round-trip", meal.get("items") == ["燕麦酸奶", "水煮蛋"], meal.get("items"))
check("meal has all nutrition fields",
      all(k in meal for k in ("protein", "fiber", "veggies", "water", "penalty")), meal)
check("meal time HH:MM", len(str(meal.get("time"))) == 5 and str(meal.get("time"))[2] == ":", meal.get("time"))
first_receipt = body.get("receipt_id")

status, default_recent = call("GET", MEALS)
check("recent source=service", default_recent.get("source") == "service", default_recent.get("source"))
check("recent has contract fields",
      all(k in default_recent for k in ("user_id", "days", "meals", "count", "source")),
      sorted(default_recent.keys()))
check("recent default days == 7", default_recent.get("days") == 7, default_recent.get("days"))
check("count == len(meals)",
      default_recent.get("count") == len(default_recent.get("meals") or []),
      default_recent.get("count"))

status, after = meals_all()
check("first write adds exactly one row", after.get("count") == count_before + 1,
      (count_before, after.get("count")))

# ── 4. 幂等：同 key 同 payload ──
status, replay = call("POST", MEALS, {"idempotency_key": key_default, "items": ["燕麦酸奶", "水煮蛋"]})
check("replay ok", replay.get("ok") is True, replay)
check("replay idempotent=true", replay.get("idempotent") is True, replay)
check("replay same receipt_id", replay.get("receipt_id") == first_receipt,
      (replay.get("receipt_id"), first_receipt))
check("replay same meal id", (replay.get("meal") or {}).get("id") == meal.get("id"),
      ((replay.get("meal") or {}).get("id"), meal.get("id")))
status, after_replay = meals_all()
check("replay does not add a row", after_replay.get("count") == count_before + 1,
      after_replay.get("count"))

# ── 5. 幂等：同 key 不同 payload ──
status, conflict = call("POST", MEALS, {"idempotency_key": key_default, "items": ["完全不同的食物"]})
check("same key different payload -> ok:false", conflict.get("ok") is False, conflict)
check("conflict error code", conflict.get("error") == "idempotency_conflict", conflict)
status, after_conflict = meals_all()
check("conflict does not add a row", after_conflict.get("count") == count_before + 1,
      after_conflict.get("count"))

# ── 6. 并发：同 key 同 payload 只落一行 ──
concurrent_key = next_key("concurrent")
payload = {"items": ["并发测试餐"], "protein": 20, "fiber": 5, "veggies": 100, "water": 300}
status, before_conc = meals_all()
count_conc = before_conc.get("count", 0)
barrier = threading.Barrier(8)
conc_results = []
conc_lock = threading.Lock()


def worker_same():
    barrier.wait()
    st, bd = call("POST", MEALS, dict(payload, idempotency_key=concurrent_key))
    with conc_lock:
        conc_results.append((st, bd))


threads = [threading.Thread(target=worker_same) for _ in range(8)]
for t in threads:
    t.start()
for t in threads:
    t.join()

check("concurrent all http 200", all(st == 200 for st, _ in conc_results),
      [st for st, _ in conc_results])
check("concurrent all ok:true", all(bd.get("ok") is True for _, bd in conc_results),
      [bd for _, bd in conc_results if not bd.get("ok")])
receipts = {bd.get("receipt_id") for _, bd in conc_results}
check("concurrent single receipt", len(receipts) == 1, receipts)
status, after_conc = meals_all()
check("concurrent adds exactly one row", after_conc.get("count") == count_conc + 1,
      (count_conc, after_conc.get("count")))

# ── 7. 并发：同 key 不同 payload 由唯一键裁决 ──
mixed_key = next_key("mixed")
status, before_mixed = meals_all()
count_mixed = before_mixed.get("count", 0)
barrier2 = threading.Barrier(8)
mixed_results = []
mixed_lock = threading.Lock()


def worker_mixed(idx):
    barrier2.wait()
    text = "混合A" if idx % 2 == 0 else "混合B"
    st, bd = call("POST", MEALS, {"idempotency_key": mixed_key, "text": text,
                                  "items": [text], "protein": 10 if idx % 2 == 0 else 12})
    with mixed_lock:
        mixed_results.append((idx, st, bd))


threads = [threading.Thread(target=worker_mixed, args=(i,)) for i in range(8)]
for t in threads:
    t.start()
for t in threads:
    t.join()

winners = [bd for _, _, bd in mixed_results if bd.get("ok") is True]
losers = [bd for _, _, bd in mixed_results if bd.get("ok") is False]
check("mixed has winners", len(winners) >= 1, len(winners))
check("mixed losers are conflicts", all(bd.get("error") == "idempotency_conflict" for bd in losers),
      [bd.get("error") for bd in losers])
check("mixed single winning receipt", len({bd.get("receipt_id") for bd in winners}) == 1,
      {bd.get("receipt_id") for bd in winners})
status, after_mixed = meals_all()
check("mixed adds exactly one row (rollback works)", after_mixed.get("count") == count_mixed + 1,
      (count_mixed, after_mixed.get("count")))

# ── 8. penalty 服务端补算 ──
status, body = call("POST", MEALS, {"idempotency_key": next_key("pen"), "text": "奶茶配炸鸡"})
check("penalty regex 4+3", (body.get("meal") or {}).get("penalty") == 7, body.get("meal"))
status, body = call("POST", MEALS, {"idempotency_key": next_key("pen"), "text": "青菜豆腐"})
check("penalty zero for clean text", (body.get("meal") or {}).get("penalty") == 0, body.get("meal"))

# ── 9. 跨日期与时区 ──
yesterday = (date.today() - timedelta(days=1)).isoformat()
status, body = call("POST", MEALS, {"idempotency_key": next_key("yd"), "date": yesterday,
                                    "type": "晚餐", "text": "昨日晚餐", "items": ["昨日晚餐"]})
check("explicit date preserved", (body.get("meal") or {}).get("date") == yesterday, body.get("meal"))
status, day1 = call("GET", f"{MEALS}?days=1")
check("days=1 excludes yesterday", all(m.get("date") == today() for m in day1.get("meals") or []),
      [m.get("date") for m in day1.get("meals") or []])
status, day2 = call("GET", f"{MEALS}?days=2")
check("days=2 includes yesterday", any(m.get("date") == yesterday for m in day2.get("meals") or []),
      [m.get("date") for m in day2.get("meals") or []])
keyset = [(m.get("date"), m.get("id")) for m in day2.get("meals") or []]
check("meals sorted by date,id", keyset == sorted(keyset), keyset[:10])

# ── 10. nutrition-gap ──
status, g = call("GET", GAP)
check("gap default date == today", g.get("date") == today(), g.get("date"))
check("gap targets exact",
      g.get("targets") == {"protein": 90, "fiber": 25, "veggies": 500, "water": 1800},
      g.get("targets"))
check("gap has contract fields",
      all(k in g for k in ("user_id", "date", "targets", "intake", "gap", "source")), sorted(g.keys()))
status, prof = call("GET", PROFILE)
check("gap water target matches profile",
      g.get("targets", {}).get("water") == prof.get("water_target"), (g.get("targets"), prof.get("water_target")))

hist = "2020-06-15"
status, before_hist = gap_of(hist)
before_intake = before_hist.get("intake") or {}
status, b1 = call("POST", MEALS, {"idempotency_key": next_key("hist"), "date": hist,
                                  "type": "午餐", "text": "历史午餐A", "items": ["历史午餐A"],
                                  "protein": 30, "fiber": 8, "veggies": 200, "water": 400})
status, b2 = call("POST", MEALS, {"idempotency_key": next_key("hist"), "date": hist,
                                  "type": "晚餐", "text": "历史晚餐B", "items": ["历史晚餐B"],
                                  "protein": 25, "fiber": 6, "veggies": 150, "water": 350})
check("historical writes ok", b1.get("ok") is True and b2.get("ok") is True, (b1, b2))
status, after_hist = gap_of(hist)
after_intake = after_hist.get("intake") or {}
check("historical intake exact",
      after_intake.get("protein") == before_intake.get("protein") + 55 and
      after_intake.get("fiber") == before_intake.get("fiber") + 14 and
      after_intake.get("veggies") == before_intake.get("veggies") + 350 and
      after_intake.get("water") == before_intake.get("water") + 750,
      (before_intake, after_intake))
check("historical gap computed",
      all(after_hist["gap"][k] == max(0, after_hist["targets"][k] - after_intake[k]) for k in
          ("protein", "fiber", "veggies", "water")), after_hist)

# ── 11. weekly-stats 与评分复刻 ──
status, w = week_of()
check("weekly week == monday", w.get("week") == week_start(), (w.get("week"), week_start()))
check("weekly has contract fields",
      all(k in w for k in ("user_id", "week", "avg_health_score", "days_recorded",
                           "three_meal_days", "points", "streak", "source")), sorted(w.keys()))
check("weekly days_recorded >= 1", (w.get("days_recorded") or 0) >= 1, w.get("days_recorded"))
check("weekly streak >= 1", (w.get("streak") or 0) >= 1, w.get("streak"))
daily = w.get("daily") or []
if daily:
    avg = round(sum(d["score"] for d in daily) / len(daily), 1)
    check("weekly avg matches daily", abs(float(w.get("avg_health_score")) - avg) < 1e-9,
          (w.get("avg_health_score"), avg))

bonus = 3 if prof.get("activity") == "较高" else 2 if prof.get("activity") == "适中" else 0
status, pre_gap = call("GET", GAP)
pre_intake = pre_gap.get("intake") or {}
status, pre_week = week_of()
pre_today = today_daily(pre_week) or {"meals": 0, "penalty": 0}
delta = {"protein": 17, "fiber": 9, "veggies": 210, "water": 260}
status, scored = call("POST", MEALS, {"idempotency_key": next_key("score"), "type": "午餐",
                                      "text": "评分校验餐", "items": ["评分校验餐"],
                                      **delta})
check("score meal written", scored.get("ok") is True, scored)
status, post_week = week_of()
post_today = today_daily(post_week)
want = expected_score(
    {k: pre_intake.get(k, 0) + delta[k] for k in delta},
    pre_today.get("meals", 0) + 1,
    pre_today.get("penalty", 0),
    bonus,
)
check("weekly score matches frontend formula",
      post_today is not None and post_today.get("score") == want,
      (post_today, want,
       {k: pre_intake.get(k, 0) + delta[k] for k in delta},
       pre_today.get("meals", 0) + 1, bonus))

# ── 12. three_meal_days：补齐今天三餐 ──
status, pre = week_of()
pre_three = pre.get("three_meal_days") or 0
pre_daily = today_daily(pre) or {"meals": 0}
existing_types = set()
for m in (call("GET", f"{MEALS}?days=1")[1].get("meals") or []):
    if m.get("date") == today():
        existing_types.add(m.get("type"))
missing = [t for t in ("早餐", "午餐", "晚餐") if t not in existing_types]
for t in missing:
    call("POST", MEALS, {"idempotency_key": next_key("three"), "type": t,
                         "text": f"三餐补齐-{t}", "items": [f"三餐补齐-{t}"],
                         "protein": 20, "fiber": 5, "veggies": 150, "water": 200})
status, post = week_of()
post_three = post.get("three_meal_days") or 0
if missing:
    check("three_meal_days increments after completing mains", post_three == pre_three + 1,
          (pre_three, post_three, missing))
else:
    check("three_meal_days consistent when mains already complete", post_three == pre_three,
          (pre_three, post_three))
status, post_daily = call("GET", f"{MEALS}?days=1")
today_meals = [m for m in post_daily.get("meals") or [] if m.get("date") == today()]
check("today has 3+ meals", len(today_meals) >= 3, len(today_meals))
check("today mains complete", {"早餐", "午餐", "晚餐"} <= {m.get("type") for m in today_meals},
      {m.get("type") for m in today_meals})
check("weekly points > 0", (post.get("points") or 0) > 0, post.get("points"))

# ── 13. 截断边界 ──
long_item = "长" * 100
status, body = call("POST", MEALS, {"idempotency_key": next_key("cut"), "items": [long_item]})
item = ((body.get("meal") or {}).get("items") or [""])[0]
check("item truncated to 60 chars", len(item) == 60, len(item))
long_text = "文" * 600
status, body = call("POST", MEALS, {"idempotency_key": next_key("cut"), "text": long_text})
check("text truncated to 512 chars", len((body.get("meal") or {}).get("text") or "") == 512,
      len((body.get("meal") or {}).get("text") or ""))
many_items = [f"菜{i}" for i in range(200)]
status, body = call("POST", MEALS, {"idempotency_key": next_key("cut"), "items": many_items})
check("many items still writable", body.get("ok") is True, body)
check("items json within 1024 bytes", len(json.dumps((body.get("meal") or {}).get("items") or [],
                                                     ensure_ascii=False).encode()) <= 1024,
      len(json.dumps((body.get("meal") or {}).get("items") or [], ensure_ascii=False).encode()))

# ── 14. 未知路由仍为统一 200 + not_found ──
status, body = call("GET", f"/api/v1/users/{UID}/unknown-thing", token=None)
check("unknown api route -> 200 not_found", status == 200 and body.get("error") == "not_found", body)

passed = sum(1 for _, ok in results if ok)
failed = len(results) - passed
print(f"SUMMARY passed={passed} failed={failed} total={len(results)}")
sys.exit(0 if failed == 0 else 1)