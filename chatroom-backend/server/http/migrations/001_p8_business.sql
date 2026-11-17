-- ============================================================================
-- P8-S1 正式迁移（仅限独立测试库 chatroom_p8）
-- P8-S0 迁移草案（未执行、未授权）。S1 授权后正式落位：
--   chatroom-backend/server/http/migrations/001_p8_business.sql
-- 前置（全部满足才可执行）：
--   ① 已确认目标库：SELECT DATABASE();（必须为预期库，如 chatroom）
--   ② 已 mysqldump 全库备份并校验可恢复
--   ③ 单实例执行（禁止并发跑两次迁移）
--   ④ 已通过 precheck（§Step 0 / Step 0B），任一不通过 → 非零退出，不得继续
--   ⑤ 本文件与 001_p8_business_rollback.draft.sql 成对使用
-- 会话约束：Step 1–5 必须在同一个 mysql 会话内执行（使用会话变量 @col/@sql/
--   @seed_pwd_hash）；跨连接会话变量为 NULL 会导致空哈希事故。
-- ============================================================================

-- ----------------------------------------------------------------------------
-- Step 0A  预检：目标表存在性（供 wrapper 判读，不产生变更）
-- ----------------------------------------------------------------------------
-- 期望：四张业务表均不存在（首次迁移）；若已存在，必须经 Step 0B 指纹校验。
SELECT TABLE_NAME, TABLE_COMMENT
  FROM information_schema.TABLES
 WHERE TABLE_SCHEMA = DATABASE()
   AND TABLE_NAME IN ('health_profile','meal_records','idempotency_records','foods')
 ORDER BY TABLE_NAME;

-- 既有结构确认（迁移只依赖 user 表；friend 表不动）
SELECT TABLE_NAME, COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, COLUMN_KEY
  FROM information_schema.COLUMNS
 WHERE TABLE_SCHEMA = DATABASE()
   AND TABLE_NAME IN ('user','friend')
 ORDER BY TABLE_NAME, ORDINAL_POSITION;

-- ----------------------------------------------------------------------------
-- Step 0B  同名表指纹校验（R21，fail-closed）
-- ----------------------------------------------------------------------------
-- 判读规则（wrapper 实现，任一命中即非零退出，库零变更）：
--   a) 表不存在                      -> 正常，可创建
--   b) 表存在 且 TABLE_COMMENT 以
--      'p8:migration=001_p8_business' 开头
--                                   -> 视为本迁移遗留（重跑/断点续跑），继续
--   c) 表存在 且 COMMENT 无该标记    -> 来源不明（他人手工建表/旧版本）
--                                   -> 立即中止，人工核查；禁止自动 DROP/重建
-- 禁止行为：任何情况下不得对来源不明的同名表执行 DROP / TRUNCATE / RENAME。
SELECT
  COUNT(*) AS existing_tables,
  SUM(CASE WHEN TABLE_COMMENT LIKE 'p8:migration=001\_p8\_business%' THEN 1 ELSE 0 END) AS marked_tables
  FROM information_schema.TABLES
 WHERE TABLE_SCHEMA = DATABASE()
   AND TABLE_NAME IN ('health_profile','meal_records','idempotency_records','foods');

-- ----------------------------------------------------------------------------
-- Step 1  user.agent_uid 条件化加列（MySQL 8 无 ADD COLUMN IF NOT EXISTS）
-- ----------------------------------------------------------------------------
SET @col := (SELECT COUNT(*) FROM information_schema.COLUMNS
              WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'user'
                AND COLUMN_NAME = 'agent_uid');
SET @sql := IF(@col = 0,
  'ALTER TABLE `user`
     ADD COLUMN `agent_uid` VARCHAR(64) NULL
       COMMENT ''p8:migration=001_p8_business; Agent 侧用户标识'',
     ADD UNIQUE KEY `uk_agent_uid` (`agent_uid`)',
  'SELECT 1');
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- 若列已存在但无迁移标记 -> 来源不明，人工核查（不自动改）
SELECT COLUMN_NAME, COLUMN_COMMENT
  FROM information_schema.COLUMNS
 WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'user' AND COLUMN_NAME = 'agent_uid';

-- ----------------------------------------------------------------------------
-- Step 2  四张业务表（全部 IF NOT EXISTS + 表级迁移标记）
-- ----------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS `health_profile` (
  `agent_uid`    VARCHAR(64)  NOT NULL,
  `name`         VARCHAR(20)  NOT NULL DEFAULT '',
  `gender`       VARCHAR(20)  NOT NULL DEFAULT '',
  `age`          INT          NOT NULL DEFAULT 0,
  `height`       DOUBLE       NOT NULL DEFAULT 0,
  `weight`       DOUBLE       NOT NULL DEFAULT 0,
  `goal`         VARCHAR(120) NOT NULL DEFAULT '均衡饮食',
  `activity`     VARCHAR(120) NOT NULL DEFAULT '久坐',
  `diet_style`   VARCHAR(20)  NOT NULL DEFAULT '普通饮食',
  `water_target` INT          NOT NULL DEFAULT 1800,
  `sleep_hours`  DOUBLE       NOT NULL DEFAULT 7,
  `allergies`    VARCHAR(1024) NOT NULL DEFAULT '[]'
                 COMMENT 'JSON 数组，API 限 ≤20 项 × ≤30 字符',
  `dislikes`     VARCHAR(1024) NOT NULL DEFAULT '[]'
                 COMMENT 'JSON 数组，API 限 ≤20 项 × ≤30 字符',
  `spend_min`    INT          NOT NULL DEFAULT 20,
  `spend_max`    INT          NOT NULL DEFAULT 35,
  `note`         VARCHAR(120) NOT NULL DEFAULT '',
  `updated_at`   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP
                 ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`agent_uid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='p8:migration=001_p8_business;schema=p8-1;owner=backend-p8';

CREATE TABLE IF NOT EXISTS `meal_records` (
  `id`         BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `agent_uid`  VARCHAR(64)  NOT NULL,
  `date`       DATE         NOT NULL,
  `type`       VARCHAR(8)   NOT NULL DEFAULT '加餐'
               COMMENT '早餐/午餐/下午茶/晚餐/加餐',
  `time`       CHAR(5)      NOT NULL DEFAULT '00:00' COMMENT 'HH:MM（本地时区）',
  `text`       VARCHAR(512) NOT NULL DEFAULT ''  COMMENT '原文，API 截断至 512 字符',
  `items`      VARCHAR(1024) NOT NULL DEFAULT '[]'
               COMMENT 'JSON 数组，API 序列化后 ≤1024 字符',
  `protein`    DOUBLE       NOT NULL DEFAULT 0,
  `fiber`      DOUBLE       NOT NULL DEFAULT 0,
  `veggies`    DOUBLE       NOT NULL DEFAULT 0,
  `water`      DOUBLE       NOT NULL DEFAULT 0,
  `penalty`    INT          NOT NULL DEFAULT 0
               COMMENT '服务端按 P8-06 §7.5 正则补算，Agent 不传',
  `created_at` DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_uid_date` (`agent_uid`,`date`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='p8:migration=001_p8_business;schema=p8-1;owner=backend-p8';

CREATE TABLE IF NOT EXISTS `idempotency_records` (
  `id`           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `agent_uid`    VARCHAR(64) NOT NULL,
  `idem_key`     VARCHAR(64) NOT NULL,
  `payload_hash` CHAR(64)    NOT NULL
                 COMMENT 'SHA256(canonical{date,type,text}) 小写 hex',
  `meal_id`      BIGINT UNSIGNED NOT NULL COMMENT '首次成功的 meal_records.id',
  `created_at`   DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_uid_key` (`agent_uid`,`idem_key`),
  KEY `idx_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='p8:migration=001_p8_business;schema=p8-1;owner=backend-p8';

CREATE TABLE IF NOT EXISTS `foods` (
  `id`          VARCHAR(64)  NOT NULL COMMENT 'slug 主键，与 MOCK_FOODS.id 一致',
  `name`        VARCHAR(64)  NOT NULL,
  `category`    VARCHAR(64)  NOT NULL DEFAULT '',
  `description` VARCHAR(255) NOT NULL DEFAULT '',
  `calories`    DOUBLE       NOT NULL DEFAULT 0 COMMENT '人工估算值，非实验室数据',
  `protein`     DOUBLE       NOT NULL DEFAULT 0,
  `fiber`       DOUBLE       NOT NULL DEFAULT 0,
  `veggies`     DOUBLE       NOT NULL DEFAULT 0,
  `water`       DOUBLE       NOT NULL DEFAULT 0,
  `nature`      VARCHAR(16)  NOT NULL DEFAULT 'recommend'
                COMMENT 'recommend|indulge',
  `keywords`    VARCHAR(255) NOT NULL DEFAULT '' COMMENT '空格分隔',
  `image`       VARCHAR(512) NOT NULL DEFAULT '',
  `meal_types`  VARCHAR(64)  NOT NULL DEFAULT '[]' COMMENT 'JSON 数组',
  `price`       INT          NOT NULL DEFAULT 0,
  `available`   TINYINT(1)   NOT NULL DEFAULT 1,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='p8:migration=001_p8_business;schema=p8-1;owner=backend-p8';

-- 创建后立刻复核标记（防止外部同名表抢先创建且 IF NOT EXISTS 静默跳过）
SELECT TABLE_NAME, TABLE_COMMENT
  FROM information_schema.TABLES
 WHERE TABLE_SCHEMA = DATABASE()
   AND TABLE_NAME IN ('health_profile','meal_records','idempotency_records','foods')
 ORDER BY TABLE_NAME;

-- ----------------------------------------------------------------------------
-- Step 3  存量回填（公式冻结：legacy_ + SHA256(email#id) 前 16 位）
-- ----------------------------------------------------------------------------
UPDATE `user`
   SET `agent_uid` = CONCAT('legacy_', LEFT(SHA2(CONCAT(`email`, '#', `id`), 256), 16))
 WHERE `agent_uid` IS NULL;

-- ----------------------------------------------------------------------------
-- Step 4  u_demo seed 预检（R16；wrapper 据结果分支；任一不匹配 → 非零退出）
-- ----------------------------------------------------------------------------
SELECT
  (SELECT COUNT(*) FROM `user` WHERE `email` = 'u_demo@agent.local') AS email_hit,
  (SELECT COUNT(*) FROM `user` WHERE `agent_uid` = 'u_demo')         AS uid_hit,
  (SELECT COUNT(*) FROM `user` WHERE `email` = 'u_demo@agent.local'
                                   AND `agent_uid` = 'u_demo')        AS same_hit;

-- ----------------------------------------------------------------------------
-- Step 5  u_demo seed（R19：摘要外部注入；SET 与 INSERT 必须同会话）
-- ----------------------------------------------------------------------------
-- wrapper 以 heredoc 注入：
--   SEED_HEX=$(openssl rand -hex 32)
--   SEED_SHA=$(printf '%s' "$SEED_HEX" | sha256sum | cut -d' ' -f1)
--   mysql --socket=/run/mysqld/mysqld.sock -uroot "$DB" <<SQL
--   SET @seed_pwd_hash = '${SEED_SHA}';
--   INSERT INTO `user` (`name`,`email`,`password`,`state`,`agent_uid`)
--   SELECT '我','u_demo@agent.local',@seed_pwd_hash,0,'u_demo' FROM DUAL
--    WHERE NOT EXISTS (SELECT 1 FROM `user` WHERE `agent_uid`='u_demo')
--      AND NOT EXISTS (SELECT 1 FROM `user` WHERE `email`='u_demo@agent.local');
--   SQL
--   unset SEED_HEX SEED_SHA
-- 本文件仅保留变量引用，不含任何明文或摘要字面量。
INSERT INTO `user` (`name`,`email`,`password`,`state`,`agent_uid`)
SELECT '我','u_demo@agent.local',@seed_pwd_hash,0,'u_demo'
  FROM DUAL
 WHERE NOT EXISTS (SELECT 1 FROM `user` WHERE `agent_uid` = 'u_demo')
   AND NOT EXISTS (SELECT 1 FROM `user` WHERE `email` = 'u_demo@agent.local');

-- ----------------------------------------------------------------------------
-- Step 6  foods seed（16 道演示数据，见 foods_seed.draft.sql；同会话执行）
-- ----------------------------------------------------------------------------
-- SOURCE 语句在 mysql CLI 下不可用于任意路径，wrapper 用 `\. ` 或直接拼接文件；
-- 也可由 wrapper 在 Step 5 后追加执行 foods_seed.draft.sql。

-- ----------------------------------------------------------------------------
-- Step 7  迁移后自检（wrapper 断言全部通过；R23 修正：索引总数 8）
-- ----------------------------------------------------------------------------
-- 7.1 列存在
SELECT COUNT(*) AS agent_uid_col
  FROM information_schema.COLUMNS
 WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'user' AND COLUMN_NAME = 'agent_uid';

-- 7.2 四表存在且带迁移标记
SELECT COUNT(*) AS marked_tables
  FROM information_schema.TABLES
 WHERE TABLE_SCHEMA = DATABASE()
   AND TABLE_NAME IN ('health_profile','meal_records','idempotency_records','foods')
   AND TABLE_COMMENT LIKE 'p8:migration=001\_p8\_business%';

-- 7.3 索引逐项（按 TABLE_NAME.INDEX_NAME 去重应为 8；STATISTICS 行数不是 8）
SELECT TABLE_NAME, INDEX_NAME, NON_UNIQUE, SEQ_IN_INDEX, COLUMN_NAME
  FROM information_schema.STATISTICS
 WHERE TABLE_SCHEMA = DATABASE()
   AND ( (TABLE_NAME = 'user'                AND INDEX_NAME = 'uk_agent_uid')
      OR (TABLE_NAME = 'health_profile'      AND INDEX_NAME = 'PRIMARY')
      OR (TABLE_NAME = 'meal_records'        AND INDEX_NAME IN ('PRIMARY','idx_uid_date'))
      OR (TABLE_NAME = 'idempotency_records' AND INDEX_NAME IN ('PRIMARY','uk_uid_key','idx_created_at'))
      OR (TABLE_NAME = 'foods'               AND INDEX_NAME = 'PRIMARY') )
 ORDER BY TABLE_NAME, INDEX_NAME, SEQ_IN_INDEX;

SELECT COUNT(DISTINCT CONCAT(TABLE_NAME, '.', INDEX_NAME)) AS index_count
  FROM information_schema.STATISTICS
 WHERE TABLE_SCHEMA = DATABASE()
   AND ( (TABLE_NAME = 'user'                AND INDEX_NAME = 'uk_agent_uid')
      OR (TABLE_NAME = 'health_profile'      AND INDEX_NAME = 'PRIMARY')
      OR (TABLE_NAME = 'meal_records'        AND INDEX_NAME IN ('PRIMARY','idx_uid_date'))
      OR (TABLE_NAME = 'idempotency_records' AND INDEX_NAME IN ('PRIMARY','uk_uid_key','idx_created_at'))
      OR (TABLE_NAME = 'foods'               AND INDEX_NAME = 'PRIMARY') );  -- 期望 8

-- 7.4 既有索引未被破坏
SELECT TABLE_NAME, INDEX_NAME, NON_UNIQUE, COLUMN_NAME
  FROM information_schema.STATISTICS
 WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'user'
   AND INDEX_NAME IN ('PRIMARY','email')
 ORDER BY INDEX_NAME, SEQ_IN_INDEX;  -- 期望 PRIMARY(id) + email 唯一(email)

-- 7.5 foods 行数与分布
SELECT COUNT(*) AS foods_total, SUM(available = 1) AS foods_available FROM `foods`;  -- 期望 16/16

-- 7.6 u_demo 行
SELECT id, name, email, state, agent_uid FROM `user` WHERE agent_uid = 'u_demo';