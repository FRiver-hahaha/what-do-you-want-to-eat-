CREATE TABLE IF NOT EXISTS `profile_media` (
  `user_key`   VARCHAR(64)      NOT NULL,
  `cover_src`  MEDIUMTEXT       NULL,
  `avatar_src` MEDIUMTEXT       NULL,
  `cover_ver`  BIGINT UNSIGNED  NOT NULL DEFAULT 0,
  `avatar_ver` BIGINT UNSIGNED  NOT NULL DEFAULT 0,
  `updated_at` DATETIME(3)      NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
                                ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`user_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='eat:migration=007_profile_media;schema=v1';

INSERT IGNORE INTO `schema_migration` (`migration_id`, `checksum`)
VALUES ('007_profile_media', SHA2('007_profile_media', 256));
