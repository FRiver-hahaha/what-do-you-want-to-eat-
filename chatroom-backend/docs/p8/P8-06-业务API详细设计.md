# P8-06 业务 API 详细设计（P8-2 修订版）

> 修订依据：R2 幂等事务、R3 删改定性、R4 评分与营养数据，其他一致性 #2/#3/#5。
> 通用约定：业务错误一律 HTTP 200 + `{ok:false,error,message}`（4xx 会触发 Agent mock 回退，见 P8-02 §1）；成功响应含 `"source":"service"`。

## 0. 错误语义总表（一致性 #5 修订，五类分别明确）

| 场景 | HTTP | 响应体 | C++ 后续动作 | Agent 实际行为（取证） |
|---|---|---|---|---|
| Token 缺失 | 200 | `{ok:false,error:"unauthorized"}` | ERROR 日志 | 上层按 dict 采用，data 无预期字段→工具摘要退化/失败文案（见 §0.1） |
| Token 错误 | 200 | 同上 | ERROR 日志含来源 IP | 同上 |
| **P4 模式 uid ≠ "u_demo"** | 200 | `{ok:false,error:"forbidden",message:"single-demo-user mode"}` | INFO 日志（uid） | 工具不检查 ok（G-10）→ 摘要退化；**不是认证失败，是演示白名单拒绝** |
| DB 故障/连接池超时 | 200 | `{ok:false,error:"internal_error",message:"db unavailable"}` | ERROR 日志 + /health db:false | 同上；注意：若进程连不上（超时/宕机）则是连接级失败→Agent 直接回退 **mock 数据**（用户可能看不到错误而看到演示数据——降级设计，写入 KNOWN-ISSUES） |
| 参数错误 | 200 | `{ok:false,error:"invalid_params",message:具体字段}` | INFO 日志 | 工具摘要可能仍显示成功（§0.1 缺口） |
| 资源不存在 | 200 | `{ok:false,error:"user_not_found"\|"not_found"}` | INFO 日志 | 同上 |

### 0.1 Agent 对 `{ok:false}` 的实际处理（取证，只记录不改）

| 工具 | 检查 `data.get("ok")`？ | 后果 |
|---|---|---|
| update_health_profile | ✅（diet_tools.py:302） | 正确报错 |
| update_spend_level | ✅（menu_tools.py:131） | 正确报错 |
| **record_meal** | ❌（diet_tools.py:_record_meal） | **服务端拒绝时工具仍返回 ok=True + "已记录XX"成功文案**（G-9，用户被误导为成功、库中无记录） |
| get_health_profile / get_recent_meals / get_nutrition_gap / get_weekly_stats / get_friend_ranking / get_friend? / search_foods / get_menu | ❌ | 错误体被当数据用：摘要出现 0/空/"暂无"（G-10，可容忍但语义失真） |

→ G-9 是**Agent 侧缺陷**，零改动约束下 C++ 只能保证"错误体规范、日志可查"；根治需 Agent 改（记录于 P8-12，等待学长决策）。

## 1. GET /api/v1/users/{uid}/health-profile

响应（字段与 P8-1 相同）：user_id, name, gender, age, height, weight, bmi(现算), goal, activity, diet_style, water_target + waterTarget(双写), sleep("X小时") + sleep_hours(数值), allergies[], dislikes[], spend_min, spend_max, note, source。
- 档案不存在但 uid 存在 → 返回**默认档案**（=MOCK_PROFILE 值，user_id 替换）；uid 不存在 → user_not_found。
- bmi = round(weight/(height/100)², 2)；缺身高/体重时 bmi=null。

## 2. PATCH /api/v1/users/{uid}/health-profile

- 白名单/校验（P8-1 §2 字段集）：height/weight ∈ (0,500)；age/water_target/spend_min/spend_max ∈ [0,9999]；spend_min≤spend_max（违反交换）；name≤20、gender/diet_style≤20、goal/activity/note≤120（截断而非拒绝）；**allergies/dislikes 每项≤30 字符且项数≤20（超出截断——P8-S0 评审发现 D-3）**。
- **uid 必须已存在**（本版取消 lazy 建户，R1 §3-C）→ user_not_found。
- 响应：{ok:true, user_id, profile(全量), updated[], source}。
- `sleep` 数值写 sleep_hours 列，文本响应现算。

## 3. GET /api/v1/users/{uid}/meals?days={n}

- **days 行为（一致性 #3 裁定）**：缺省 7；非整数 → invalid_params；**越界 clamp 到 1..30**（不拒绝），响应 `days` 字段 = **实际生效值**（与查询窗口一致）。与 Agent 契约不冲突（工具 schema 已限 1..30，正常不会触发；Agent 响应字段消费点为 0，返回 clamp 值安全）。
- 排序 date ASC, id ASC；响应结构同 P8-1 §3。

## 4. POST /api/v1/users/{uid}/meals —— 幂等事务重新设计（R2）

### 4.1 原设计的矛盾（审查确认）

- 原顺序"先 INSERT idem(meal_id) 后 INSERT meal"→ meal_id 尚未产生，NOT NULL 无法满足；如靠预分配则存在孤立账本风险。**原设计不可实现。**

### 4.2 修订方案

**表变更**（P8-07 同步）：`idempotency_records` 增加 `payload_hash CHAR(64) NOT NULL`。**R10 定稿：规范化范围 = 业务身份字段 {date, type, text}，不含 protein/fiber/veggies/water（LLM 估算值可能与重试不同）**。**canonical 规范串（实现必须逐字节一致——P8-S0 评审发现 D-5）**：键按字典序 date < text < type，形如 `{"date":"2026-11-16","text":"<原文，保留首尾空格，标准 JSON 字符串转义>","type":"午餐"}`；UTF-8 无 BOM、无多余空白、无尾换行；payload_hash = 小写 hex SHA256(该串)。仅服务端内部使用（Agent 只传 key），实现后以单元测试锁定。

**执行顺序（单事务，先 meal 后 idem）**：

```sql
BEGIN;
-- ① 写饮食记录，取得 last_insert_id
INSERT INTO meal_records(agent_uid,date,type,time,text,items,protein,fiber,veggies,water,penalty)
VALUES(?,?,?,?,?,?,?,?,?,?,?);
SET @meal_id = LAST_INSERT_ID();
-- ② 写幂等账本（(uid,key) 唯一；冲突即 1062）
INSERT INTO idempotency_records(agent_uid,idem_key,payload_hash,meal_id)
VALUES(?,?,?,@meal_id);
COMMIT;
```

**失败/冲突处理伪代码**：

```
function create_meal(uid, payload, key):
    hash = sha256(canonical_json({date: payload.date, type: payload.type, text: payload.text}))  # R10：仅业务身份字段
    # 快路径（仅减少无谓事务，非正确性依赖）
    row = SELECT meal_id,payload_hash FROM idempotency_records WHERE agent_uid=uid AND idem_key=key
    if row:
        if row.payload_hash == hash: return ok(result_of(row.meal_id))   # 同 key 同 payload → 首次结果
        else: return conflict(key)                                       # 同 key 不同 payload → 明确拒绝
    BEGIN
        meal_id = INSERT meal_records(...)            # ①
        TRY: INSERT idempotency_records(uid,key,hash,meal_id)   # ②
        COMMIT
        return ok(result_of(meal_id))
    CATCH DuplicateKey(1062):                          # 并发：另一事务已占 key
        ROLLBACK                                        # 本事务 meal 一并回滚，无孤立记录
        row = SELECT meal_id,payload_hash ... (READ COMMITTED 下可见已提交行)
        if row and row.payload_hash == hash: return ok(result_of(row.meal_id))
        else: return conflict(key)
    CATCH other: ROLLBACK; raise internal_error
```


### 4.2.1 R10 幂等一致性用例矩阵（唯一最终规则）

| # | 场景 | key | 业务身份 hash | 营养字段 | 服务端结果 | 依据 |
|---|---|---|---|---|---|---|
| 1 | 同 key、同 payload（纯重试） | 同 | 同 | 同 | 返回首次结果（receipt_id 不变） | (uid,key) 命中 + hash 同 |
| 2 | 同 key、不同 text | 同（人为构造/碰撞） | **异** | — | `idempotency_conflict` 拒绝 | hash 含 text |
| 3 | 同 key、同 text、不同 protein/fiber/…（LLM 重新估算） | 同 | **同**（营养值不入 hash） | 异 | **返回首次结果（首次营养值为权威）** | R10 核心：重试语义稳定 |
| 4 | 不同 key、相同 payload（同日同餐两次真实记录） | 异 | 同 | 同 | **两条记录**（两次真实操作） | key 不同即不同操作 |
| 5 | 并发同 key 重试 | 同 | 同 | 可能异 | 唯一键裁决 → 库中 1 行；后到者返回首次结果 | P8-08 §6 时序 |

**与 Agent 现有 key 生成的匹配性（必须解释）**：
- Agent：`key = sha1(session_id + date + type + text)[:16]`（diet_tools.py）——**key 已内含 text**，因此"同 key 不同 text"只可能出现在碰撞/非 Agent 调用（仍须拒绝）。
- 服务端 hash 取 {date,type,text} 与 Agent key 的业务维度**完全一致** → 同一次操作（同 session/date/type/text）无论 LLM 重试时营养估算如何波动，hash 恒同 → 返回首次结果，**不会误判冲突、不会产生第二条**。
- 营养值以**首次落库**为准；Agent 前端同步用工具参数而非响应 meal（G-11：与权威值可能差异，登记于 P8-13）。

**并发时序图（同 key 两请求）**：

```
T1: BEGIN → INSERT meal(A) → INSERT idem(key→A) [持 key 唯一锁] → COMMIT
T2: BEGIN → INSERT meal(B) → INSERT idem(key→B) [等待 T1 锁]
       ├─ T1 COMMIT 后 → T2 得 1062 → ROLLBACK(meal B 消失) → SELECT → 回 A
       └─ T1 ROLLBACK(异常) → T2 插入成功 → B 成为唯一有效记录
```
- InnoDB 唯一键插入阻塞行为保证串行化；两请求最终只有一条 meal 有效（A 或 B），响应 receipt 一致。
- 锁等待上限：会话 `innodb_lock_wait_timeout` 建议设 3s（T1 仅两条 INSERT，正常微秒级）；超时 → internal_error（Agent 5s 内可感知连接级失败→mock，风险登记）。
- **重启安全**：idem 与 meal 同库持久，重启后快路径直接命中（幂等跨重启成立）。
- **同 key 不同 payload 明确拒绝**（R2 要求）：响应 `{ok:false,error:"idempotency_conflict",message:"same idempotency_key with different payload"}`；注意 G-9：record_meal 不检查 ok → 用户仍会看到"已记录"文案（Agent 侧缺陷，如实登记）。

**数据库断言（P8-10 L3 必须执行）**：

```sql
-- 幂等重放后
SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo';            -- 期望：1（非 2）
SELECT COUNT(*) FROM idempotency_records WHERE agent_uid='u_demo' AND idem_key='K'; -- 期望：1
-- 失败注入（第二个 INSERT 前 kill 进程，或对 idem 表加触发器抛错）后重启
SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo' AND date=D;  -- 期望：0（无孤立 meal）
SELECT COUNT(*) FROM idempotency_records WHERE idem_key='K2';          -- 期望：0（无孤立账本）
-- 并发 8 线程同 key 后
SELECT COUNT(*) FROM meal_records WHERE agent_uid='u_demo' AND date=D;  -- 期望：1
```

- receipt_id = `yyyyMMdd-HHmmss-{meal_id}`（幂等重放同值）。
- **事务与超时（R8）**：本事务仅两条 INSERT（正常毫秒级）；Agent 5s 超时/客户端断开**不会取消已执行 SQL**，事务仍可能提交——靠幂等键保证重试不重复 + 事务原子性保证不半途（P8-08 §2.3 五问裁定）。
- 服务端补默认：date 空→今天（本地时区）；time→写入时刻；penalty→按 §7 规则补算；items 空→[text]。**长度防护：text≤512 字符、items 序列化后≤1024 字符（超出截断并在日志记录——P8-S0 评审发现 D-4）**。

### 4.3 显式声明

**本轮 C++ 只实现 10 个既有契约接口。** 不新增 DELETE/PATCH meals（见 §5 R3）。

## 5. R3 裁定：饮食记录删除/修改（原 G-1 澄清）

| 行为 | 现状（代码取证） | 服务端是否闭环 |
|---|---|---|
| 添加 | Agent record_meal → ServiceClient → POST meals | ✅ 闭环（本轮实现） |
| 读取 | Agent get_recent_meals / 报告工具 → GET meals | ✅ 闭环 |
| **删除** | Agent delete_meal 返回 `{meal_action:{action:"delete"},source:"frontend"}` → 前端 app.js:628 `applyMealAction` **只改 localStorage** | ❌ **服务端无操作、无接口调用，永不闭环** |
| **修改** | Agent update_meal 同构（new_type/new_text）→ 前端本地改 | ❌ 同上 |

| C++ 本轮允许实现的接口 | 结论 |
|---|---|
| DELETE / PATCH /api/v1/users/{uid}/meals/{id} | **不实现**：ServiceClient 无对应方法，Agent 永不调用；即便实现也无人使用，归属不明（meal_id 前端不知道） |
| 预留设计（不编码） | 保留在本文档附录，标注"待学长授权 Agent 改动后方可启用" |

**宣称纪律**：禁止在任何文档/报告写"删除修改链路已完成"。当前事实 = **删除/修改仅存于浏览器 localStorage；服务端记录只增不减不改**。统计接口（gap/weekly/ranking）对"已删但服务端仍在"的记录照常计入——这是契约缺口（G-1）的直接后果，非 C++ 缺陷。根治需学长授权改 Agent（delete_meal/update_meal 改为调 ServiceClient + 新增 Agent 方法 + 前端需知道 meal_id）。

## 6. GET /api/v1/users/{uid}/nutrition-gap?date={d}

- intake = 当日 meal_records 四字段求和（penalty 不参与缺口）。
- targets = {protein:90, fiber:25, veggies:500, water: 档案 water_target(默认 1800)}；gap=max(0,target-intake)。
- 响应结构同 P8-1 §5。

## 7. R4 修订：评分与营养数据（算法追踪与归属）

### 7.1 前端真实算法（app.js:getHealthAnalysis，取证原文）

```
targets = {protein:90, fiber:25, veggies:500, water: profile.waterTarget}
ratios  = min(intake[key]/targets[key], 1)
activityBonus = activity=="较高"?3 : activity=="适中"?2 : 0
score = clamp(45, 96, round(68 + r.veggies*8 + r.protein*6 + r.fiber*5
                            + min(meals.length,3) + activityBonus - nutrition.penalty))
status ≥86 很好 / ≥76 均衡 / ≥66 调整 / 其余关注
```
intake 由 getNutrition() 聚合本地 meals（protein/fiber/veggies/water/**penalty**）。

### 7.2 原 P8-1 拟定算法 = **错误**（自查结论）

原方案（base 60 + 18/8/8/6 权重 + clamp 0-100）与前端公式**非同源**，仅数值量级相近——正是审查 R4 禁止的做法。**本版废弃，改为逐项复刻前端公式。**

### 7.3 修订后服务端算法（严格复刻 §7.1）

```
当日 score = clamp(45, 96, round(68 + r.veggies*8 + r.protein*6 + r.fiber*5
                                  + min(meal_count,3) + activityBonus - penalty_sum))
其中 intake/penalty_sum 来自该日 meal_records；activity 来自 health_profile.activity
```

### 7.4 评分输入输出对照表

| 输入 | 前端来源 | Agent mock 来源 | 服务端来源 | 是否同源 |
|---|---|---|---|---|
| protein/fiber/veggies/water | 本地 meals（analyzeMealText 正则估算） | mock meals 常量 | meal_records（值来自 Agent 工具 payload = LLM/用户描述估算） | **否**（三方输入生成机制不同） |
| penalty | analyzeMealText 正则（炸/奶茶等） | mock 常量 0 | **本版新增服务端补算**（§7.5） | 规则同源、文本输入不同 |
| targets | 硬编码 90/25/500 + waterTarget | 同 | 同（唯一权威值写在此表） | 是 |
| activityBonus | profile.activity 文案 | 无 | health_profile.activity 文案 | 是（映射表相同） |
| score 公式 | §7.1 | mock 固定 82.4（无公式） | §7.3 复刻 | 公式同源 |

**诚实结论**：公式可 100% 同源复刻；但**输入数值的生成机制不同**（前端正则估算 vs Agent LLM 估算），因此同一用户在"前端显示的 score"与"服务端 weekly-stats 的 avg_health_score"可能不同。服务端承诺：**公式同源 + 输入以服务端落库值为准**；不承诺与浏览器实时显示逐分一致（前端仍按本地数据算，契约禁止改前端）。

### 7.5 服务端 penalty 补算规则（与 analyzeMealText 同款正则语义）

写入时对 text/items 拼接串执行：含 `奶茶|可乐|汽水|甜饮` → +4；含 `炸|薯条|炸鸡|烧烤|肥肉` → +3；上限每日 12，存 meal_records.penalty（每条记录取自身补算值）。protein 等**以 Agent 传入为准**（不覆盖）。

### 7.6 计算归属表（哪些归 Agent，哪些归 C++，哪些归前端）

| 计算 | 归属 | 说明 |
|---|---|---|
| LLM 推理/意图/文案/建议/报告撰写 | Agent | C++ **不实现任何 LLM 能力** |
| match_score（菜品匹配度 20-98） | Agent（diet_tools.match_score） | C++ 只提供菜品字段与缺口数据 |
| 菜品 daily/other 分组、70/30 随机 | Agent（menu.py） | C++ 只提供 price/档案区间 |
| 菜单导入 | Agent（SQLite menu_imports） | C++ 不感知 |
| 今日营养聚合、缺口、周统计、评分、排行 | **C++** | 数据在 MySQL，唯一权威聚合点 |
| 本地即时显示（时间线/分数动画） | 前端 | 前端本地公式，独立于服务端 |
| penalty 正则补算 | C++（新增）；前端各自算 | 同规则 |

### 7.7 食物营养数据来源声明（一致性 #10）

- foods 表 = **仅 16 道演示菜品**（与 service_client.py MOCK_FOODS 逐字段一致），**不是完整食品数据库**；calories/protein/fiber/veggies/water/price 均为人工估算值，**无份量（serving size）、无单位标准化、无过敏原（allergen）字段**。
- 过敏/忌口过滤 = 对 name+description 的**字符串包含匹配**（对齐 mock 语义，脆弱：仅当描述文本出现过敏词才生效）。
- 真正的营养数据库/过敏原体系属未来 schema 扩展，不在本轮范围（列入 KNOWN-ISSUES）。

## 8. GET /api/v1/users/{uid}/weekly-stats

- week=本周一；days_recorded/three_meal_days/streak 定义同 P8-1 §6；
- **avg_health_score = 按 §7.3 逐日计算后取平均**（1 位小数）；
- **points = Σ每日**(早/午/晚各出现 10 分 + 三者齐全 +20)——对齐 app.js getMealPoints；
- 响应结构不变。

## 9. GET /api/v1/users/{uid}/ranking?scope=

- 候选 = 本人 ∪ 双向 status=2 好友；score = §7.3（week: 本周均值；today: 当日）；降序+uid 字典序；rank 连续；本人必在榜。

## 10. GET /api/v1/users/{uid}/friends、GET /api/v1/foods/search、GET /api/v1/menu

- 结构/过滤规则同 P8-1 §8/§9（search 过敏过滤对齐 mock；menu=默认 16 道，meal_type 过滤）。

## 附录：删改接口预留（不实现）

`PATCH /api/v1/users/{uid}/meals/{id}`、`DELETE /api/v1/users/{uid}/meals/{id}`：请求/响应语义已在本修订版外部分析，**待学长授权 Agent 侧改造后**再启用（启用条件写入 P8-12）。