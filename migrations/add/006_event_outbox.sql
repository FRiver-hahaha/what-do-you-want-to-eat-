CREATE TABLE IF NOT EXISTS `schema_migration` (
  `migration_id` VARCHAR(64) NOT NULL,
  `applied_at`   DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `checksum`     CHAR(64)    NOT NULL,
  PRIMARY KEY (`migration_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='eat:migration=000_schema_migration;schema=v1';

CREATE TABLE IF NOT EXISTS `event_outbox` (
  `user_key`   CHAR(26)    NOT NULL,
  `seq`        BIGINT UNSIGNED NOT NULL,
  `event_type` VARCHAR(32) NOT NULL,
  `ref_kind`   VARCHAR(16) NOT NULL DEFAULT '',
  `ref_id`     VARCHAR(64) NOT NULL DEFAULT '',
  `created_at` DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`user_key`, `seq`),
  KEY `idx_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='eat:migration=006_event_outbox;schema=v1';

INSERT IGNORE INTO `schema_migration` (`migration_id`, `checksum`)
VALUES ('006_event_outbox', SHA2('006_event_outbox', 256));