import hashlib
import os
import sys
import time
from datetime import date

sys.path.insert(0, os.environ.get("P8_WORK_ROOT", "/home/m/eat-p8-s1"))

from agent.data.service_client import ServiceClient  # noqa: E402

client = ServiceClient(base_url="http://127.0.0.1:18080", token="local-test-token")
results = []


def check(name, cond, detail=""):
    results.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else " :: " + str(detail)))


UID = "u_demo"

# ── get_recent_meals（Agent 真实方法）──
recent = client.get_recent_meals(UID, 7)
check("recent: no mock fallback", recent.get("source") == "service", recent.get("source"))
check("recent: contract keys",
      {"user_id", "days", "meals", "count", "source"} <= set(recent), sorted(recent))
check("recent: days echo", recent.get("days") == 7, recent.get("days"))
meals = recent.get("meals") or []
check("recent: meals non-empty", len(meals) > 0, len(meals))
check("recent: count matches", recent.get("count") == len(meals), recent.get("count"))
if meals:
    meal = meals[-1]
    need = {"date", "type", "time", "text", "items", "protein", "fiber", "veggies", "water"}
    check("recent: meal fields complete", need <= set(meal), sorted(meal))
    check("recent: items list[str]",
          isinstance(meal.get("items"), list) and all(isinstance(x, str) for x in meal["items"]),
          meal.get("items"))
    check("recent: nutrition int-convertible",
          all(int(float(meal[k])) >= 0 for k in ("protein", "fiber", "veggies", "water")), meal)

# ── get_nutrition_gap ──
gap = client.get_nutrition_gap(UID)
check("gap: no mock fallback", gap.get("source") == "service", gap.get("source"))
check("gap: contract keys",
      {"user_id", "date", "targets", "intake", "gap", "source"} <= set(gap), sorted(gap))
check("gap: targets keys",
      set(gap.get("targets") or {}) == {"protein", "fiber", "veggies", "water"}, gap.get("targets"))
check("gap: arithmetic consistent",
      all(gap["gap"][k] == max(0, gap["targets"][k] - gap["intake"][k])
          for k in ("protein", "fiber", "veggies", "water")), gap)
check("gap: date is today", gap.get("date") == date.today().isoformat(), gap.get("date"))

# ── get_weekly_stats ──
weekly = client.get_weekly_stats(UID)
check("weekly: no mock fallback", weekly.get("source") == "service", weekly.get("source"))
check("weekly: contract keys",
      {"user_id", "week", "avg_health_score", "days_recorded", "three_meal_days",
       "points", "streak", "source"} <= set(weekly), sorted(weekly))
check("weekly: numeric fields",
      all(isinstance(weekly.get(k), (int, float))
          for k in ("avg_health_score", "days_recorded", "three_meal_days", "points", "streak")),
      weekly)

# ── record_meal（按 Agent 的 payload 组装方式）──
key = "agent-" + hashlib.sha1(f"contract-{time.time()}".encode("utf-8")).hexdigest()[:16]
payload = {
    "date": "",
    "type": "加餐",
    "text": "Agent 契约校验餐",
    "items": ["Agent 契约校验餐"],
    "protein": 12,
    "fiber": 3,
    "veggies": 80,
    "water": 200,
}
first = client.record_meal(UID, payload, key)
check("record: no mock fallback", first.get("source") == "service", first.get("source"))
check("record: ok true", first.get("ok") is True, first)
check("record: contract keys",
      {"ok", "receipt_id", "user_id", "meal", "source"} <= set(first), sorted(first))
recorded = first.get("meal") or {}
check("record: empty date defaults today",
      recorded.get("date") == date.today().isoformat(), recorded.get("date"))
check("record: meal fields complete",
      {"date", "type", "text", "items", "protein", "fiber", "veggies", "water"} <= set(recorded),
      sorted(recorded))

replay = client.record_meal(UID, payload, key)
check("record: replay same receipt", replay.get("receipt_id") == first.get("receipt_id"),
      (replay.get("receipt_id"), first.get("receipt_id")))
check("record: replay idempotent", replay.get("idempotent") is True, replay)
check("record: client stayed in service mode", client.mode == "service", client.mode)
check("record: no transport error", client.last_error == "", client.last_error)

# ── get_profile（顺带确认 S1 契约未被破坏）──
profile = client.get_profile(UID)
check("profile: no mock fallback", profile.get("source") == "service", profile.get("source"))
check("profile: water_target present", "water_target" in profile, sorted(profile))

passed = sum(1 for _, ok in results if ok)
failed = len(results) - passed
print(f"SUMMARY passed={passed} failed={failed} total={len(results)}")
sys.exit(0 if failed == 0 else 1)