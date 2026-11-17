DROP TABLE IF EXISTS `event_outbox`;
DELETE FROM `schema_migration` WHERE `migration_id` = '006_event_outbox';