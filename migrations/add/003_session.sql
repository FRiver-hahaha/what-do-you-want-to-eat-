CREATE TABLE IF NOT EXISTS `session` (
  `session_id`        BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id`        BIGINT UNSIGNED NOT NULL,
  `session_hash`      CHAR(64)     NOT NULL,
  `refresh_hash`      CHAR(64)     NOT NULL,
  `access_expires_at` DATETIME     NOT NULL,
  `refresh_expires_at` DATETIME    NOT NULL,
  `device_id`         VARCHAR(64)  NOT NULL DEFAULT '',
  `device_name`       VARCHAR(64)  NOT NULL DEFAULT '',
  `status`            TINYINT      NOT NULL DEFAULT 1,
  `replaced_by`       CHAR(64)     NULL,
  `client_version`    VARCHAR(32)  NOT NULL DEFAULT '',
  `created_at`        DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_seen_at`      DATETIME     NULL,
  `revoked_at`        DATETIME     NULL,
  `revoke_reason`     VARCHAR(32)  NOT NULL DEFAULT '',
  PRIMARY KEY (`session_id`),
  UNIQUE KEY `uk_session_hash` (`session_hash`),
  UNIQUE KEY `uk_refresh_hash` (`refresh_hash`),
  KEY `idx_account_status` (`account_id`,`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='eat:migration=003_session;schema=v1';

INSERT IGNORE INTO `schema_migration` (`migration_id`, `checksum`)
VALUES ('003_session', SHA2('003_session', 256));