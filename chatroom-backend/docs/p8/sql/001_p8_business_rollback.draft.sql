-- ============================================================================
-- DRAFT ONLY — 禁止执行（P8-S0 草案）
-- P8-S0 回滚草案（R21：只删本次迁移创建的对象 + 同名表 fail-closed）
-- 正式落位（S1 授权后）：chatroom-backend/server/http/migrations/001_p8_business_rollback.sql
--
-- 前置（全部满足才可执行）：
--   ① 已 mysqldump 全库备份（回滚同样会丢数据：4 张业务表全部内容）
--   ② 已停止 server_http（无在线写入）
--   ③ 已确认目标库：SELECT DATABASE();
--   ④ 已人工确认本次迁移的 manifest 文件（migration_manifest_<UTC>.txt）
--
-- 安全模型（双重证据）：
--   S1  COMMENT 标记：每张由本迁移创建的表/列均带
--       'p8:migration=001_p8_business' 前缀；无标记 = 不是本迁移对象 → 拒绝删。
--   S2  manifest 文件：迁移 wrapper 输出本次实际创建对象清单；回滚前人工比对
--       （SQL 无法读文件，由 wrapper 逐项核对后调用本脚本）。
-- 任一校验失败 → SIGNAL 45000 中止，事务内不做任何 DROP。
-- ============================================================================

-- ----------------------------------------------------------------------------
-- Step R0  预检（只读；wrapper 先执行并留证）
-- ----------------------------------------------------------------------------
SELECT TABLE_NAME, TABLE_COMMENT
  FROM information_schema.TABLES
 WHERE TABLE_SCHEMA = DATABASE()
   AND TABLE_NAME IN ('health_profile','meal_records','idempotency_records','foods')
 ORDER BY TABLE_NAME;

SELECT COLUMN_NAME, COLUMN_COMMENT
  FROM information_schema.COLUMNS
 WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'user' AND COLUMN_NAME = 'agent_uid';

-- 本次迁移将删除的 u_demo 行（默认不删；如需删除见 Step R4 的人工分支）
SELECT id, name, email, state, agent_uid
  FROM `user` WHERE email = 'u_demo@agent.local';

-- 行数快照（回滚前留证：证明删除范围仅限业务表）
SELECT
  (SELECT COUNT(*) FROM `user`)   AS user_rows,
  (SELECT COUNT(*) FROM `friend`) AS friend_rows;

-- ----------------------------------------------------------------------------
-- Step R1–R3  带校验的 DROP（存储过程内先校验后删）
-- ----------------------------------------------------------------------------
DROP PROCEDURE IF EXISTS `p8_rollback_001`;

DELIMITER $$
CREATE PROCEDURE `p8_rollback_001`()
BEGIN
  DECLARE v_bad INT DEFAULT 0;
  DECLARE v_col_bad INT DEFAULT 0;

  -- 校验 1：存在的目标表必须全部带迁移标记（无标记 → 来源不明 → 拒绝）
  SELECT COUNT(*) INTO v_bad
    FROM information_schema.TABLES
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME IN ('health_profile','meal_records','idempotency_records','foods')
     AND TABLE_COMMENT NOT LIKE 'p8:migration=001\_p8\_business%';
  IF v_bad > 0 THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT = 'P8 rollback refused: unmarked same-name table detected';
  END IF;

  -- 校验 2：agent_uid 列若存在，必须带迁移标记
  SELECT COUNT(*) INTO v_col_bad
    FROM information_schema.COLUMNS
   WHERE TABLE_SCHEMA = DATABASE()
     AND TABLE_NAME = 'user'
     AND COLUMN_NAME = 'agent_uid'
     AND COLUMN_COMMENT NOT LIKE 'p8:migration=001\_p8\_business%';
  IF v_col_bad > 0 THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT = 'P8 rollback refused: unmarked agent_uid column detected';
  END IF;

  -- 校验 3：friend 表不得被本脚本触碰（存在性断言，仅用于留证）
  SELECT COUNT(*) INTO v_bad
    FROM information_schema.TABLES
   WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'friend';
  IF v_bad <> 1 THEN
    SIGNAL SQLSTATE '45000'
      SET MESSAGE_TEXT = 'P8 rollback refused: friend table state unexpected';
  END IF;

  -- 删除顺序：幂等账本 → 餐记录 → 档案 → 菜品（无外键，顺序仅为可读性）
  DROP TABLE IF EXISTS `idempotency_records`;
  DROP TABLE IF EXISTS `meal_records`;
  DROP TABLE IF EXISTS `health_profile`;
  DROP TABLE IF EXISTS `foods`;

  -- 列回滚：先删索引（若在），再删列（若在）；两步独立判定，避免人工删过索引后报错
  IF EXISTS (SELECT 1 FROM information_schema.STATISTICS
              WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'user'
                AND INDEX_NAME = 'uk_agent_uid') THEN
    SET @s := 'ALTER TABLE `user` DROP INDEX `uk_agent_uid`';
    PREPARE ps FROM @s; EXECUTE ps; DEALLOCATE PREPARE ps;
  END IF;

  IF EXISTS (SELECT 1 FROM information_schema.COLUMNS
              WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'user'
                AND COLUMN_NAME = 'agent_uid') THEN
    SET @s := 'ALTER TABLE `user` DROP COLUMN `agent_uid`';
    PREPARE ps FROM @s; EXECUTE ps; DEALLOCATE PREPARE ps;
  END IF;
END$$
DELIMITER ;

CALL `p8_rollback_001`();
DROP PROCEDURE IF EXISTS `p8_rollback_001`;

-- ----------------------------------------------------------------------------
-- Step R4  u_demo seed 行处置（默认保留，需人工显式选择）
-- ----------------------------------------------------------------------------
-- 默认（本脚本行为）：保留 `u_demo@agent.local` 行，仅删除 agent_uid 列 →
--   该行退化为普通 TCP 用户（随机密码，无法登录），不造成数据丢失。
--   注意：保留该行后立即重跑迁移，Step 4 预检会判
--   email_hit=1 AND same_hit=0 → 非零退出（fail-closed，属预期保护）。
--
-- 需要"回滚到干净状态"时，人工执行（先备份该行）：
--   SELECT * FROM `user` WHERE email='u_demo@agent.local';        -- 留证
--   DELETE FROM `user` WHERE email='u_demo@agent.local'
--     AND name='我' AND state=0 LIMIT 1;                          -- 人工确认后
-- 自动脚本绝不包含该 DELETE（R19 规则延伸）。

-- ----------------------------------------------------------------------------
-- Step R5  回滚后自检（wrapper 断言）
-- ----------------------------------------------------------------------------
-- 期望：4 表不存在；agent_uid 列不存在；user/friend 行数与 Step R0 快照一致
--       （选择 Step R4 人工删行时，user_rows 减 1，须在证据中注明）
SELECT TABLE_NAME FROM information_schema.TABLES
 WHERE TABLE_SCHEMA = DATABASE()
   AND TABLE_NAME IN ('health_profile','meal_records','idempotency_records','foods');  -- 期望 0 行

SELECT COUNT(*) AS agent_uid_col
  FROM information_schema.COLUMNS
 WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME='user' AND COLUMN_NAME='agent_uid';   -- 期望 0

SELECT (SELECT COUNT(*) FROM `user`) AS user_rows,
       (SELECT COUNT(*) FROM `friend`) AS friend_rows;

-- 启动自检预期：server_http 因缺 agent_uid 列 → FATAL 退出（P8-07 §7.2），
-- 确保残缺结构不会对外提供服务。