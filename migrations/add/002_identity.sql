CREATE TABLE IF NOT EXISTS `schema_migration` (
  `migration_id` VARCHAR(64) NOT NULL,
  `applied_at`   DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `checksum`     CHAR(64)    NOT NULL,
  PRIMARY KEY (`migration_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='eat:migration=000_schema_migration;schema=v1';

CREATE TABLE IF NOT EXISTS `account` (
  `account_id`      BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `user_key`        CHAR(26)     NOT NULL,
  `login_id`        VARCHAR(128) NOT NULL,
  `display_name`    VARCHAR(50)  NOT NULL DEFAULT '',
  `status`          TINYINT      NOT NULL DEFAULT 1,
  `password_digest` VARCHAR(255) NOT NULL,
  `revoke_epoch`    INT UNSIGNED NOT NULL DEFAULT 0,
  `registered_at`   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `last_login_at`   DATETIME     NULL,
  `failed_attempts` TINYINT      NOT NULL DEFAULT 0,
  `locked_until`    DATETIME     NULL,
  PRIMARY KEY (`account_id`),
  UNIQUE KEY `uk_user_key` (`user_key`),
  UNIQUE KEY `uk_login_id` (`login_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='eat:migration=002_identity;schema=v1';

CREATE TABLE IF NOT EXISTS `account_alias` (
  `alias_id`        BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `account_id`      BIGINT UNSIGNED NOT NULL,
  `alias_type`      VARCHAR(16)  NOT NULL,
  `normalized_value` VARCHAR(128) NOT NULL,
  PRIMARY KEY (`alias_id`),
  UNIQUE KEY `uk_alias` (`alias_type`,`normalized_value`),
  KEY `idx_account` (`account_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='eat:migration=002_identity;schema=v1';

INSERT IGNORE INTO `schema_migration` (`migration_id`, `checksum`)
VALUES ('002_identity', SHA2('002_identity', 256));