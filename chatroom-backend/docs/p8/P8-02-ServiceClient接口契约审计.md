# P8-02 ServiceClient 接口契约审计（P8-2 修订版）

> 审计对象：agent/data/service_client.py（main fc34b0f）。
> 方法：逐接口核对 HTTP 方法/路径/参数/Header/认证/Schema/错误处理/超时/幂等，并与 AGENT.md 11.3、agent/tools/*.py 的实际消费字段交叉验证。

## 1. 通用契约（_request，service_client.py:196-219）

| 项 | 实测值 |
|---|---|
| Base URL | EAT_SERVER_BASE_URL（rstrip /；空=mock 模式） |
| 认证 | `Authorization: Bearer {EAT_SERVER_TOKEN}`（token 为空则不带该头） |
| Content-Type | `application/json; charset=utf-8`（仅带 body 的请求） |
| 超时 | EAT_SERVICE_TIMEOUT（默认 5s，urllib 单次，无重试） |
| 代理 | 显式禁用（ProxyHandler({})） |
| 成功判定 | HTTP 2xx 且响应体为合法 JSON；dict 直接采用（并注入 source:"service"），非 dict 包一层 {data:…, source:"service"} |
| 失败判定 | URLError/HTTPError/Timeout/OSError/JSONDecodeError → 返回 None → 上层回退 mock |
| 字符编码 | 请求 UTF-8（ensure_ascii=False）；响应按 UTF-8 解码 |

**关键推论（C++ 实现硬约束）：**
1. HTTP 4xx/5xx 都会触发 Agent 回退 mock —— C++ 的业务错误**必须以 2xx + JSON 错误体**返回（见 §5 错误码设计），否则 Agent 无法区分"服务端拒绝"与"服务端宕机"。
2. 响应必须是合法 JSON；Content-Length 必须准确（urllib 依赖）。
3. 5s 超时意味着 C++ 单请求处理（含 SQL）必须 < 5s。

## 2. 接口契约清单（10 个已实现 + 1 个文档预留）

### 2.1 GET /api/v1/users/{uid}/health-profile

| 项 | 值 |
|---|---|
| Query | 无 |
| 请求体 | 无 |
| 响应（Agent 消费字段，含 mock 对齐） | user_id, name, height, weight, age, gender, goal, activity, water_target, waterTarget（双写）, sleep（"X小时"）, sleep_hours（数值）, allergies[], dislikes[], diet_style, spend_min, spend_max, bmi, note, source |
| 消费方 | diet_tools.merged_profile → _profile 摘要 / menu_tools.spend_band / report_tools 报告；agent-adapter.js syncLocalProfile |
| 幂等 | 天然 |

### 2.2 PATCH /api/v1/users/{uid}/health-profile

| 项 | 值 |
|---|---|
| 请求体 | 字段白名单子集：name(≤20)/gender/diet_style(≤20)/goal/activity(≤120)/note(≤120)/height/weight(float)/age/water_target/spend_min/spend_max(int)/sleep(数值→双字段)/allergies[]/dislikes[]（ diet_tools.py:_update_health_profile 预处理） |
| 响应 | {ok:bool, user_id, profile(最新全量，同 2.1 结构), updated[](字段名列表), source} |
| Agent 依赖 | data["ok"] 必须为真值；data["profile"] 供前端 syncLocalProfile；data.get("error") 失败时读取 |
| 幂等 | 末次写入生效（无 key） |
| 注意 | 空 fields 请求 Agent 侧已拦截（missing_fields），C++ 仍需防御 |

### 2.3 GET /api/v1/users/{uid}/meals?days={n}

| 项 | 值 |
|---|---|
| Query | days（int，缺省 7；非整数→invalid_params；越界 **clamp 1..30**，响应 days=实际生效值；P8-06 §3） |
| 响应 | {user_id, days, meals[{date(YYYY-MM-DD), type(早餐/午餐/下午茶/晚餐/加餐), time(HH:MM), text, items[], protein, fiber, veggies, water, penalty}], count, source} |
| 排序 | mock 为时间正序（今日早→晚，历史穿插）；C++ 按date ASC, id ASC |
| 消费方 | diet_tools.recent_meals（摘要取最后一条）、report_tools（渲染近 N 天） |

### 2.4 GET /api/v1/users/{uid}/nutrition-gap?date={d}

| 项 | 值 |
|---|---|
| Query | date（YYYY-MM-DD，Agent 默认今天，本地时区） |
| 响应 | {user_id, date, targets{protein:90, fiber:25, veggies:500, water:档案 water_target（默认 1800）}, intake{...当日聚合}, gap{max(0, target-intake)}, source} |
| 消费方 | diet_tools.merged_gap（client_context.today.nutrition 仅 mock 模式覆盖）、match_score |

### 2.5 GET /api/v1/users/{uid}/weekly-stats

| 项 | 值 |
|---|---|
| Query | 无 |
| 响应 | {user_id, week(本周一 YYYY-MM-DD), avg_health_score(float), days_recorded(int), three_meal_days(int), points(int), streak(int), source} |
| 语义 | 本周（周一至今）：days_recorded=有记录的自然日数；three_meal_days=早/午/晚三餐齐全天数；streak=截至今天（或最近记录日）的连续记录天数；points 与积分体系对齐（三餐各 10 + 齐全 20，前端 app.js getMealPoints 同源口径） |

### 2.6 GET /api/v1/users/{uid}/ranking?scope={week|today}

| 项 | 值 |
|---|---|
| Query | scope（默认 week） |
| 响应 | {user_id, scope, ranking[{rank(int 1..n), user_id(字符串 agent_uid), name, score(float)}], source} |
| 范围 | 仅本人 + 好友（friend status=2 双向正常）；本人必须入榜（mock 中 u_demo rank 2 在榜） |

### 2.7 GET /api/v1/users/{uid}/friends

| 项 | 值 |
|---|---|
| 响应 | {user_id, friends[{id(字符串), name, relation:"friend"}], source} |

### 2.8 GET /api/v1/foods/search?q=&meal_type=&limit=&nature=

| 项 | 值 |
|---|---|
| Query | q（URL 编码，空格分词）、meal_type（枚举）、limit（1-16，Agent 默认 5）、nature（recommend|indulge|空） |
| **未上送** | tags（Agent 工具有此参数，service 调用未传——G-3 关联） |
| 服务端职责 | 关键词匹配（name/category/description/keywords，空格分词 AND 语义按命中数排序）+ meal_type/nature 过滤 + available=true + **该用户过敏/忌口过滤**（对齐 mock 行为）+ limit 截断 |
| 响应 | {user_id, foods[{id, name, category, description, calories, protein, fiber, veggies, water, nature, keywords, meal_types[], price, available, image}], count, source} |
| Agent 后处理 | 每道菜加 match(20-98)（diet_tools.match_score），不改食物字段 |

### 2.9 GET /api/v1/menu?meal_type={t}

| 项 | 值 |
|---|---|
| Query | meal_type（可空=全部） |
| 响应 | {user_id, meal_type(请求值或"全部"), foods[同 2.8 结构], count, source} |
| 优先级 | Agent 先查本地 menu_imports（导入菜单），命中则不调此接口（menu_tools.current_menu:22-38）——C++ 此接口=**默认菜单** |

### 2.10 POST /api/v1/users/{uid}/meals

| 项 | 值 |
|---|---|
| 请求体 | {date(YYYY-MM-DD，可空=今天), type(五枚举), text, items[], protein, fiber, veggies, water, idempotency_key(16 hex)} |
| 幂等（R2/R10 修订） | (uid, idem_key) 唯一 + payload_hash：**payload_hash = SHA256(canonical{date,type,text})（业务身份字段，不含 protein/fiber/veggies/water 等 LLM 估算值）**；同 key 同业务身份 → 首次结果（营养值以首次为准）；同 key 不同 text（人为构造）→ 明确拒绝 idempotency_conflict；不同 key 相同 payload → 两条记录（两次真实操作）；并发同 key 仅 1 条；写入顺序先 meal 后 idem（P8-06 §4.2 R10 规则） |
| 响应 | {ok:true, receipt_id(服务端生成，持久稳定), user_id, meal{date,type,text,items,protein,fiber,veggies,water[,time,penalty]}, source} |
| 服务端补默认 | date 空→今天（本地时区）；time→写入时刻 HH:MM；**penalty→服务端按 P8-06 §7.5 正则补算（非 0）**；items 空→[text] |

### 2.11 GET /api/v1/skills/market（文档预留，未实现）

AGENT.md:826-853 列出但 ServiceClient 无对应方法（G-2）。**本轮不实现**，仅在路由层预留 404 → 触发 Agent 回退（其本身也不会调用）。

## 3. 跨接口字段审计结论

| 审计项 | 结论 |
|---|---|
| user_id 类型 | 全链路字符串（"u_demo"/"web_xxx"/任意 localStorage 值）；C++ 需 agent_uid 字符串键，禁止 int 强转 |
| 双写字段 | water_target+waterTarget、sleep("X小时")+sleep_hours 必须同时输出（G-4） |
| bmi | profile 读取响应需输出（Agent 档案摘要/报告直接使用；mock 由身高体重计算） |
| 日期 | 全部 YYYY-MM-DD 本地时区（G-7）；time 为 HH:MM |
| 空值/默认 | 响应字段尽量完整非 null（mock 全字段有值）；int 字段用 0，数组用 []，字符串用 "" |
| source | Agent 覆盖为 "service"（setdefault）——C++ 返回任意/缺失均可，建议 "service" |
| 错误体 | 2xx + {ok:false, error:code, message:…}（§5） |
| 越权 | Token 校验 + uid 存在性校验；ranking/friends 限好友范围（P8-05） |

## 4. Agent 实际依赖矩阵（响应字段 → 消费点）

| 响应字段 | 消费点（缺字段后果） |
|---|---|
| profile.name/height/weight/goal/activity/allergies/dislikes/water_target/waterTarget/sleep | 档案摘要、报告、菜单分组、缺口目标（缺失→显示 "-" 或 1800 默认） |
| meals[].date/type/text | 报告渲染、摘要"最近一条"（缺失→记录不可读） |
| nutrition gap targets/intake/gap | 缺口摘要 + match_score（缺失→match 失真） |
| weekly days_recorded/avg_health_score/points/streak/three_meal_days | 周报摘要（缺失→显示 0/'-'） |
| ranking[].name/score | 排行摘要第一名（空榜→"暂无"） |
| foods[].name/price/veggies/protein/fiber/calories/nature | match_score 与菜单渲染（缺失→match=低分） |
| record ok/receipt_id/meal | 写入确认（ok=false → Agent 工具报 update_failed 类错误） |

## 5. C++ 错误码设计（对齐"2xx 错误体"约束）

| error 码 | HTTP | 场景 | Agent 行为 |
|---|---|---|---|
| unauthorized | 200 | Bearer Token 缺失/错误 | data 无 ok 字段 → 上层按 dict 采用并 mock 回退（需返回 {ok:false}） |
| user_not_found | 200 | uid 不存在 | 同上 |
| invalid_params | 200 | 参数校验失败 | 同上 |
| not_found | 200 | 资源不存在 | 同上 |
| idempotency_conflict | 200 | 同 key 不同 payload | 工具不检查 ok（G-9）→ 用户仍见成功文案（Agent 侧缺陷，P8-12） |
| internal_error(db unavailable) | 200 | 池耗尽/DB 宕 | 工具不检查 ok（G-10）→ 摘要退化；连接级失败→mock 回退 |
| internal_error | 200 | SQL 异常 | {ok:false,error:"internal_error"} |

注：HTTP 层仍可能因进程崩溃返回连接错误 → Agent mock 回退（这是可接受的降级路径，但不应成为常态）。
unauthorized 若返回 401 也会触发 mock 回退——为让运维可区分"配置错"，C++ 日志必须记录鉴权失败明细。

**Agent 对 {ok:false} 的实际检查行为（取证）**：仅 update_health_profile（diet_tools.py:302）与 update_spend_level（menu_tools.py:131）检查；record_meal 与全部 get_* 不检查（G-9/G-10，P8-12 登记）。