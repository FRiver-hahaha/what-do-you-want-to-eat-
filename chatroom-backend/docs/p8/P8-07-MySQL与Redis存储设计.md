# P8-07 MySQL 与 Redis 存储设计（P8-2 修订版）

> 修订依据：R2 幂等表结构、R4 数据来源、其他一致性 #2 注销、#8 迁移安全、#9 表数量、#10 演示数据。
> 表数量口径（#9 修正）：**新增 4 张表**（health_profile / meal_records / idempotency_records / foods）+ **user 表新增 1 列**（agent_uid）。此前文档"5 表/6 表"表述统一废止。
> **目标库现状兼容（P8-S0 评审发现 D-1）**：迁移对两类目标库均可执行——① P7 init.sql 库（user/friend 2 表）；② 历史遗留库（实测 chatroom 库仍含 message/file/group_info/group_user 4 张旧表，未清理）。迁移只加列/建新表/回填/seed，**不删除任何旧表**；旧表清理另行决策（登记 K-4）。
> **外键策略（P8-S0 评审发现 D-2）**：4 张新表**有意不建 FOREIGN KEY**——user.agent_uid 为 UNIQUE 非主键（FK 语义绕行）、与 TCP 账号域（int id）解耦、避免 DDL 锁与跨表写放大；完整性由应用层（Auth 白名单 + DAO 事务）+ 唯一键保证；idempotency_records.meal_id 亦不建 FK（账本与 meal 均只增，未来启用删改时另行评估）。

## 1. ER 总览

```
user (既有, +agent_uid 列) 1───* health_profile (agent_uid)
      │ 1───* meal_records (agent_uid)
      │ 1───* idempotency_records (agent_uid)
      └ friend (既有表, status=2)
foods (全局, 16 道演示数据)
```

## 2. 表结构（迁移设计稿，本轮不执行）

### 2.1 user 加列 + 回填 + seed（#8 迁移安全修订）

```sql
-- ① 幂等加列（information_schema 前置检查，见 §7 可重复执行方案）
ALTER TABLE `user`
  ADD COLUMN `agent_uid` VARCHAR(64) NULL COMMENT 'Agent 侧用户标识',
  ADD UNIQUE KEY `uk_agent_uid` (`agent_uid`);

-- ② 存量回填：确定性哈希，避免 email 前缀短名冲突（原前缀方案废止）
UPDATE `user`
   SET `agent_uid` = CONCAT('legacy_', LEFT(SHA2(CONCAT(`email`,'#',`id`), 256), 16))
 WHERE `agent_uid` IS NULL;

-- ③ u_demo seed（R13 + R16 + R19 修订）
--    state=0 = 离线/可登录（tryLogin: UPDATE state=1 WHERE state=0），**不是禁用标志**
--    R19 摘要传递规则（必须遵守）：
--      a) 明文用 OS CSPRNG 生成：openssl rand -hex 32（256-bit）
--      b) 摘要：printf '%s' "$SEED_HEX" | sha256sum | cut -d' ' -f1（小写 hex 64 位，与 TCP sha256() 同格式）
--      c) **SET @seed_pwd_hash 与本 INSERT 必须在同一个 MySQL 会话内执行**
--         （不同连接会话变量不共享：第二个连接里 @seed_pwd_hash 为 NULL → 写入空密码哈希）
--      d) 明文不写 SQL 文件 / 不写日志 / 不进 Git；脚本执行后立即 unset
--      e) 本 SQL 文件仅保留 @seed_pwd_hash 变量引用，不含任何明文或摘要字面量
--
--    可执行模板（运维，一次性；单会话 heredoc 保证 c 条）：
--      SEED_HEX=$(openssl rand -hex 32)
--      SEED_SHA=$(printf '%s' "$SEED_HEX" | sha256sum | cut -d' ' -f1)
--      mysql --socket=/run/mysqld/mysqld.sock -uroot chatroom <<SQL
--      SET @seed_pwd_hash = '${SEED_SHA}';
--      INSERT INTO `user` (`name`,`email`,`password`,`state`,`agent_uid`)
--      SELECT '我','u_demo@agent.local',@seed_pwd_hash,0,'u_demo' FROM DUAL
--       WHERE NOT EXISTS (SELECT 1 FROM `user` WHERE `agent_uid`='u_demo')
--         AND NOT EXISTS (SELECT 1 FROM `user` WHERE `email`='u_demo@agent.local');
--      SQL
--      unset SEED_HEX SEED_SHA
--
--    R16 seed 冲突保护（先判后写，绝不覆盖已有账号身份）：
--      都不存在            -> 创建
--      两者均指向同一用户   -> 视为已初始化，跳过
--      email 存在但 agent_uid 不匹配 -> 迁移停止，人工处理
--      agent_uid='u_demo' 存在但 email 不匹配 -> 迁移停止，人工处理
--    （预检 SQL 见 §7.1.1；任何"停止"分支由迁移脚本以非零退出码显式中断）
--
--    R19 失败清理与重试：
--      - INSERT 为单语句原子：失败（约束/语法）→ 无残留，修正后重跑预检→重试即可；
--      - 若 seed 成功但后续迁移步骤失败：用户重跑迁移 → 预检 same_hit=1 → 跳过 seed（不再生成新摘要，旧摘要保留）；
--      - 极端重来（例如误建了错误的 u_demo 行）：属人工高危操作——先备份，人工确认后删除该行再重跑；
--        **自动脚本绝不包含该删除动作**；
--      - 每次重试前必须重跑 §7.1.1 预检并重新判定分支。
INSERT INTO `user` (`name`,`email`,`password`,`state`,`agent_uid`)
SELECT '我','u_demo@agent.local',@seed_pwd_hash,0,'u_demo'
  FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `user` WHERE `agent_uid`='u_demo')
   AND NOT EXISTS (SELECT 1 FROM `user` WHERE `email`='u_demo@agent.local');
```
- 回填哈希含 id：同 email 前缀不冲突；UNIQUE 键安全。
- seed 双条件防 email/uid 冲突；重复执行无副作用（NOT EXISTS 双保险）。
- **u_demo 登录语义（R7 代码取证结论）**：
  - signIn 真实验证逻辑 = 查 email → 比对 `sha256(输入密码)` → 匹配且 `tryLogin`（state 0→1）成功即登录。**state=0 不是禁用，是"可登录"。**
  - 旧方案 `SHA2('u-demo-seed',256)` **是可计算哈希**——任何知道该字符串的人都能用密码 `u-demo-seed` 完成 TCP 登录。**旧文档"不可 TCP 登录"结论错误，已废止。**
  - 修订（R13/R19）：**废止 `UUID()+RAND()`**（非密码学安全）——改由运维用**操作系统 CSPRNG（openssl rand -hex 32，256-bit）**在迁移准备阶段生成明文，仅计算 SHA-256 摘要入库；**SET @seed_pwd_hash 与 INSERT 必须同一 MySQL 会话**（跨连接会话变量为 NULL）；明文不写 SQL/日志/Git，用后即销毁；失败无残留、重试走预检跳过；TCP 登录代码**零改动**（沿用既有 sha256 比对）。
  - 验证（P8-10 L2.07）：尝试以 `u-demo-seed` 及常见密码 TCP 登录 `u_demo@agent.local` → **必须全部失败**（"密码错误"）。
  - P4 模式下 u_demo 的 HTTP 访问不依赖 TCP 登录（Auth 白名单），随机密码不阻塞演示。
- **注册流程不写 agent_uid**（P7 代码不动）；新注册用户与 Agent uid 的桥接属治理层问题（P8-05 §5，阻断 BLK-1）。

### 2.2 health_profile（同 P8-1，主键=agent_uid）

```sql
CREATE TABLE IF NOT EXISTS `health_profile` (
  `agent_uid` VARCHAR(64) NOT NULL,
  `name` VARCHAR(20) NOT NULL DEFAULT '',
  `gender` VARCHAR(20) NOT NULL DEFAULT '',
  `age` INT NOT NULL DEFAULT 0,
  `height` DOUBLE NOT NULL DEFAULT 0,
  `weight` DOUBLE NOT NULL DEFAULT 0,
  `goal` VARCHAR(120) NOT NULL DEFAULT '均衡饮食',
  `activity` VARCHAR(120) NOT NULL DEFAULT '久坐',
  `diet_style` VARCHAR(20) NOT NULL DEFAULT '普通饮食',
  `water_target` INT NOT NULL DEFAULT 1800,
  `sleep_hours` DOUBLE NOT NULL DEFAULT 7,
  `allergies` VARCHAR(1024) NOT NULL DEFAULT '[]' COMMENT 'JSON 数组（API 限 ≤20 项×30 字符）',
  `dislikes` VARCHAR(1024) NOT NULL DEFAULT '[]',
  `spend_min` INT NOT NULL DEFAULT 20,
  `spend_max` INT NOT NULL DEFAULT 35,
  `note` VARCHAR(120) NOT NULL DEFAULT '',
  `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`agent_uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
```
- 默认值 = MOCK_PROFILE（首日体验一致）；sleep 文本响应现算，不落列。
- profile.name 与 user.name 不做同步（P8-05 §7）。

### 2.3 meal_records（同 P8-1 + penalty 语义明确）

结构不变；`penalty` 列由服务端按 P8-06 §7.5 规则补算写入（Agent 不传该字段）。`time` 默认写入时刻 HH:MM；`date` 为本地时区 DATE。

### 2.4 idempotency_records（R2 修订：+payload_hash）

```sql
CREATE TABLE IF NOT EXISTS `idempotency_records` (
  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `agent_uid` VARCHAR(64) NOT NULL,
  `idem_key` VARCHAR(64) NOT NULL,
  `payload_hash` CHAR(64) NOT NULL COMMENT 'SHA256(canonical {date,type,text}) — R10：不含营养估算值',
  `meal_id` BIGINT UNSIGNED NOT NULL COMMENT '首次成功的 meal_records.id',
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_uid_key` (`agent_uid`,`idem_key`),
  KEY `idx_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
```
- `meal_id NOT NULL` 矛盾已解决：写入顺序改为"先 meal 后 idem"（P8-06 §4.2），写入 idem 时 meal_id 必然已存在。
- payload_hash 支撑"同 key 不同 payload 明确拒绝"。
- 保留 30 天（运维清理 SQL，§6）；清理按 `idx_created_at` 走索引（P8-S0 评审发现 D-6，避免全表扫）。

### 2.5 foods（16 道演示数据，字段同 P8-1）

```sql
-- 结构同 P8-1 §2.5（id/name/category/description/calories/protein/fiber/veggies/
-- water/nature/keywords/image/meal_types/price/available）
```
- **演示数据集声明（#10）**：仅 16 行，与 MOCK_FOODS 1:1；数值为人工估算；无 serving/unit/allergen 列。
- 搜索在服务内存完成（<100 行），SQL 只 `SELECT * WHERE available=1`。

## 3. 事务与锁（R2 定稿）

| 操作 | 顺序 | 冲突处理 |
|---|---|---|
| POST meals | ①INSERT meal → ②INSERT idem(uk 冲突) → COMMIT | 1062 → ROLLBACK → 回读已提交 idem：hash 同→首次结果；异→idempotency_conflict |
| PATCH profile | 单条 UPSERT | 行锁 |
| 其余 | 单 SELECT | — |

会话级 `innodb_lock_wait_timeout=3`（连接池 init 后 SET；防止锁等待超过 Agent 5s 超时窗口）。

## 4. R4 数据来源与字段级声明

| 字段组 | 来源 | 性质 |
|---|---|---|
| meal protein/fiber/veggies/water | Agent payload（LLM 从用户描述估算） | 估算值，非实验室数据 |
| meal penalty | 服务端正则补算（P8-06 §7.5） | 规则估算 |
| foods 全部营养字段 | MOCK_FOODS 人工估算 | **演示数据** |
| targets (90/25/500/water_target) | 契约硬编码 + 档案 | 唯一权威定义（P8-06 §7.4） |

## 5. 注销与删除策略（#2 修订，如实）

| 事件 | 现状行为 | P8 处理 |
|---|---|---|
| TCP 注销（type=7） | 删除 user + friend（P7 既有） | **不级联** health_profile/meal_records/idempotency（不改 ChatService）。→ 产生孤儿数据。**登记为 K-2 已知限制**；根治需改 ChatService（属 P7 代码，需另行授权） |
| 幂等账本 | — | 30 天定时清理（`tools/cleanup_idempotency.sql`，运维 cron） |
| meal_records | — | 只增（G-1 决定删改不落服务端）；归档由运维 |
| foods | — | 运维手工 seed 更新 |

**K-2 影响评估**：注销后重新注册同 email 会获得新 user.id 与新 legacy agent_uid（若走注册），旧 agent_uid 数据成为不可达孤儿（借 Token 仍可读——见 BLK-1）。数据不泄露给其他自然人，仅占存储；风险等级：低。列入 KNOWN-ISSUES。

## 6. Redis 角色（不变）

仅 P7 既有 offline_request（TCP 域）；HTTP 进程不连 Redis；不用缓存/限流/幂等/发布订阅。

## 7. 迁移脚本可重复执行与回滚（#8 修订）

### 7.1 可重复执行方案（MySQL 8 无 ADD COLUMN IF NOT EXISTS）

```sql
-- 001_p8_business.sql 顶部：加列条件化（存储过程或应用层检查）
SET @col := (SELECT COUNT(*) FROM information_schema.columns
             WHERE table_schema=DATABASE() AND table_name='user' AND column_name='agent_uid');
SET @sql := IF(@col=0,
  'ALTER TABLE `user` ADD COLUMN `agent_uid` VARCHAR(64) NULL, ADD UNIQUE KEY `uk_agent_uid` (`agent_uid`)',
  'SELECT 1');
PREPARE stmt FROM @sql; EXECUTE stmt; DEALLOCATE PREPARE stmt;
```
- 4 张 CREATE TABLE 均 IF NOT EXISTS；回填 UPDATE 仅 WHERE agent_uid IS NULL；seed 双 NOT EXISTS。
- 验证方式：连续执行两遍退出码 0、第二遍零变更（P8-10 L3.41）。

### 7.1.1 u_demo seed 预检 SQL（R16，迁移脚本前置执行）

```sql
-- 统计三种冲突态（迁移脚本据结果分支，任一"不匹配"以非零退出码中断）
SELECT
  (SELECT COUNT(*) FROM `user` WHERE `email`='u_demo@agent.local') AS email_hit,
  (SELECT COUNT(*) FROM `user` WHERE `agent_uid`='u_demo') AS uid_hit,
  (SELECT COUNT(*) FROM `user` WHERE `email`='u_demo@agent.local' AND `agent_uid`='u_demo') AS same_hit;
```
判读：`email_hit=0 AND uid_hit=0` → 创建；`same_hit=1` → 已初始化跳过；`email_hit=1 AND same_hit=0` → **停止（人工）**；`uid_hit=1 AND same_hit=0` → **停止（人工）**。**不自动覆盖、不改写已有行身份。**

### 7.2 回滚脚本与安全边界

```sql
-- 001_p8_business_rollback.sql
DROP TABLE IF EXISTS `idempotency_records`,`meal_records`,`health_profile`,`foods`;
-- agent_uid 列回滚：EXPLICITLY 需人工确认（会丢失 uid 映射与 u_demo 绑定）
-- ALTER TABLE `user` DROP INDEX `uk_agent_uid`, DROP COLUMN `agent_uid`;
```
- 回滚会丢失：全部业务数据（4 表）+ 全部 agent_uid 映射（重加列需重跑回填，u_demo 数据成孤儿）。
- **回滚前强制 mysqldump**（部署文档红线）；回滚后 /health 必须报 db:true 且业务接口返回 user_not_found（列缺失时 DAO 需启动自检失败 → 进程拒绝启动更好——设计：main_http 启动时检查 agent_uid 列存在，缺则 FATAL 退出）。
- 既有 email 冲突：seed 已双条件防；回填哈希含 id 不冲突；若迁移前库中已有 agent_uid 列（异常环境）→ 条件化语句跳过，人工核对。

## 8. 权限

运行期 chatserver 账号仅 SELECT/INSERT/UPDATE/DELETE（新表 DML 在既有权限内）；迁移用 root 一次性执行 DDL。迁移后立即回收 root 使用（部署文档步骤）。