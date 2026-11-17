CREATE TABLE IF NOT EXISTS `friend_relation` (
  `relation_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  `owner_key`   CHAR(26)    NOT NULL,
  `peer_key`    CHAR(26)    NOT NULL,
  `status`      TINYINT     NOT NULL DEFAULT 0,
  `blocked_by`  CHAR(26)    NULL,
  `requested_at` DATETIME   NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at`  DATETIME    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`relation_id`),
  UNIQUE KEY `uk_owner_peer` (`owner_key`,`peer_key`),
  KEY `idx_owner_status` (`owner_key`,`status`),
  KEY `idx_peer_status` (`peer_key`,`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='eat:migration=004_friend_relation;schema=v1';

INSERT IGNORE INTO `schema_migration` (`migration_id`, `checksum`)
VALUES ('004_friend_relation', SHA2('004_friend_relation', 256));