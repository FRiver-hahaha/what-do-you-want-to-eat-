# Migration Notes

## Order

1. `001_p8_business.sql` (P8 baseline, existing)
2. `002_identity.sql`
3. `003_session.sql`
4. `004_friend_relation.sql`
5. `005_meal_partition.sql`
6. `006_event_outbox.sql`

Each step has a paired `_rollback.sql`.

## Preconditions

- MySQL 8.0+, InnoDB, utf8mb4.
- Full backup and restore verification before execution.
- `SELECT DATABASE();` must match the intended database.
- Single migration session; no concurrent execution.

## Partition constraint

MySQL requires every unique key on a partitioned table to include the partitioning column.

- `meal_records`: primary key becomes `(id, date)`; partitioning by `TO_DAYS(date)`.
- `idempotency_records`: partitioning by `TO_DAYS(created_day)`, where `created_day` is a stored generated column from `created_at`; the uniqueness key becomes `(user_key, idem_key, created_day)`.

The idempotency key therefore remains unique within a day. A key reused after the idempotency window has expired is outside the window and is allowed. Within the window, the same key is blocked.

## Rollback limits

Rollback only removes objects and columns created by these migrations. It does not touch P7 `user` / `friend` tables or existing business data. `meal_records` rollback restores the single-column primary key and removes partitioning.