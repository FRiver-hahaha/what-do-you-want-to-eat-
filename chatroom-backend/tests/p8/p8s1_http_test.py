import json
import sys
import urllib.error
import urllib.request

BASE = "http://127.0.0.1:18080"
TOKEN = "local-test-token"
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
UID = "u_demo"
PROFILE_PATH = f"/api/v1/users/{UID}/health-profile"

results = []


def check(name, cond, detail=""):
    results.append((name, cond))
    print(("PASS " if cond else "FAIL ") + name + ("" if cond else " :: " + str(detail)))


def call(method, path, body=None, token=TOKEN):
    data = None if body is None else json.dumps(body, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(BASE + path, data=data, method=method)
    req.add_header("Content-Type", "application/json; charset=utf-8")
    if token is not None:
        req.add_header("Authorization", "Bearer " + token)
    try:
        with OPENER.open(req, timeout=15) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        return exc.code, json.loads(exc.read().decode("utf-8") or "{}")


# 1. 健康检查（免认证）
status, body = call("GET", "/health", token=None)
check("health 200 + ok", status == 200 and body.get("ok") is True, (status, body))
check("health database=ok", body.get("database") == "ok", body)

# 2. 认证
status, body = call("GET", PROFILE_PATH, token=None)
check("no token -> unauthorized", body.get("error") == "unauthorized", (status, body))
status, body = call("GET", PROFILE_PATH, token="wrong-token")
check("wrong token -> unauthorized", body.get("error") == "unauthorized", (status, body))
status, body = call("GET", PROFILE_PATH.replace("Bearer", ""), token=None)
check("missing header -> unauthorized", body.get("error") == "unauthorized", body)

# 3. P4 白名单（先于 DAO / 用户存在性）
status, body = call("GET", "/api/v1/users/u_other/health-profile")
check("other uid -> forbidden", body.get("error") == "forbidden", body)
check("other uid message", body.get("message") == "single-demo-user mode", body)
status, body = call("GET", "/api/v1/users/u_other/health-profile", token=None)
check("other uid without token -> unauthorized first", body.get("error") == "unauthorized", body)

# 4. UID 形状校验
status, body = call("GET", "/api/v1/users/u.demo/health-profile")
check("invalid uid chars -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("GET", "/api/v1/users/" + ("a" * 65) + "/health-profile")
check("overlong uid -> invalid_params", body.get("error") == "invalid_params", body)

# 5. 默认档案（未写入数据库时）
status, body = call("GET", PROFILE_PATH)
check("GET default profile name", body.get("name") == "我", body)
check("GET default profile bmi 20.76", body.get("bmi") == 20.76, body.get("bmi"))
check("GET source=service", body.get("source") == "service", body)
check("GET water dual write", body.get("water_target") == 1800 and body.get("waterTarget") == 1800, body)
check("GET sleep text", body.get("sleep") == "7小时" and body.get("sleep_hours") == 7, body)
check("GET allergies array", body.get("allergies") == ["花生"], body)

# 6. PATCH 成功并持久化
status, body = call("PATCH", PROFILE_PATH, {"height": 175, "weight": 70})
check("PATCH ok", body.get("ok") is True, body)
check("PATCH bmi recomputed", body.get("bmi") == 22.86, body.get("bmi"))
check("PATCH updated fields", body.get("updated") == ["height", "weight"], body.get("updated"))
status, body = call("GET", PROFILE_PATH)
check("GET persisted height/weight", body.get("height") == 175 and body.get("weight") == 70, body)

# 7. 字段校验
status, body = call("PATCH", PROFILE_PATH, {"height": 999})
check("height out of range -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("PATCH", PROFILE_PATH, {"height": 0})
check("height 0 -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("PATCH", PROFILE_PATH, {"age": "abc"})
check("age non-numeric -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("PATCH", PROFILE_PATH, {"gender": 123})
check("gender non-string -> invalid_params", body.get("error") == "invalid_params", body)
status, body = call("PATCH", PROFILE_PATH, {})
check("empty object -> invalid_params", body.get("error") == "invalid_params", body)

# 8. 截断而非拒绝
status, body = call("PATCH", PROFILE_PATH, {"note": "x" * 200})
check("note truncated to 120", body.get("ok") is True and len(body.get("note", "")) == 120, len(body.get("note", "")))
long_item = "花" * 40
status, body = call("PATCH", PROFILE_PATH, {"allergies": [long_item] * 25})
allergies = body.get("allergies", [])
check("allergies capped to 20 items", len(allergies) == 20, len(allergies))
check("allergy item truncated to 30 chars", len(allergies[0]) == 30 if allergies else False, len(allergies[0]) if allergies else 0)

# 9. spend 交换与双写 / sleep 文本
status, body = call("PATCH", PROFILE_PATH, {"spend_min": 50, "spend_max": 10})
check("spend swapped", body.get("spend_min") == 10 and body.get("spend_max") == 50, body)
status, body = call("PATCH", PROFILE_PATH, {"waterTarget": 2000})
check("waterTarget writes water_target", body.get("water_target") == 2000 and body.get("waterTarget") == 2000, body)
status, body = call("PATCH", PROFILE_PATH, {"sleep": "8小时"})
check("sleep text parsed", body.get("sleep_hours") == 8 and body.get("sleep") == "8小时", body)

# 10. 非法 JSON
req = urllib.request.Request(BASE + PROFILE_PATH, data=b"{not-json", method="PATCH")
req.add_header("Authorization", "Bearer " + TOKEN)
req.add_header("Content-Type", "application/json")
try:
    with OPENER.open(req, timeout=15) as resp:
        status, body = resp.status, json.loads(resp.read().decode("utf-8"))
except urllib.error.HTTPError as exc:
    status, body = exc.code, json.loads(exc.read().decode("utf-8") or "{}")
check("invalid json -> invalid_params", body.get("error") == "invalid_params", body)

# 11. 未实现路由仍为统一 404
# 注：S3 已正式实现 /api/v1/menu，改用仍未注册的路径，断言语义不变。
status, body = call("GET", "/api/v1/not-implemented", token=None)
check("unknown api route -> not_found (200)", status == 200 and body.get("error") == "not_found", body)

passed = sum(1 for _, ok in results if ok)
failed = len(results) - passed
print(f"SUMMARY passed={passed} failed={failed} total={len(results)}")
sys.exit(0 if failed == 0 else 1)