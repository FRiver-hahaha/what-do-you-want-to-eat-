ALTER TABLE `idempotency_records`
  REMOVE PARTITIONING;

ALTER TABLE `idempotency_records`
  DROP INDEX `uk_uid_key_day`,
  ADD UNIQUE KEY `uk_uid_key` (`user_key`, `idem_key`),
  DROP COLUMN `created_day`,
  DROP PRIMARY KEY,
  ADD PRIMARY KEY (`id`);

ALTER TABLE `idempotency_records`
  DROP COLUMN `user_key`;

ALTER TABLE `meal_records`
  REMOVE PARTITIONING;

ALTER TABLE `meal_records`
  DROP INDEX `idx_uid_date_id`,
  DROP PRIMARY KEY,
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_uid_date` (`user_key`, `date`);

ALTER TABLE `meal_records`
  DROP COLUMN `deleted_at`,
  DROP COLUMN `updated_at`,
  DROP COLUMN `version`,
  DROP COLUMN `user_key`;

DELETE FROM `schema_migration` WHERE `migration_id` = '005_meal_partition';