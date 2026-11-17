DROP TABLE IF EXISTS `account_alias`;
DROP TABLE IF EXISTS `account`;
DELETE FROM `schema_migration` WHERE `migration_id` = '002_identity';