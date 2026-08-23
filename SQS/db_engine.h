/* db_engine.h -- tiny flat-file SQL-subset storage engine.
 *
 * Deliberately simple, deliberately NOT a real database: one table = one
 * flat text file, rewritten in full on every write. This is step one of
 * a "roll your own database later" plan -- the point of this file is the
 * shape of the native<->PHP boundary (a standalone .sqo, callable by
 * name, that never touches the PHP interpreter's own data structures),
 * not raw throughput or SQL completeness.
 *
 * Supported SQL subset (see db_engine.c's own top comment for the exact
 * grammar): CREATE TABLE, INSERT INTO ... VALUES, SELECT ... [WHERE
 * col=val AND ...] [ORDER BY col [ASC|DESC]] [LIMIT n], UPDATE ... SET
 * ... [WHERE ...], DELETE FROM ... [WHERE ...]. WHERE only supports "="
 * equality, ANDed -- no OR/LIKE/IN/JOIN/subqueries. Out of scope on
 * purpose; the caller (the $wpdb-compatible PHP class) never asks for
 * more than this.
 */
#ifndef SQS_DB_ENGINE_H
#define SQS_DB_ENGINE_H

/* Sets the directory table files live in (created if missing). Must be
 * called once before any other sqdb_* call. */
void sqdb_open(const char *data_dir);

/* Runs one CREATE TABLE / INSERT / UPDATE / DELETE statement. Returns the
 * number of rows created/affected (0 is a valid, successful result for
 * CREATE TABLE), or -1 on error with a human-readable message written to
 * `err` (err may be NULL to discard it). */
int sqdb_exec(const char *sql, char *err, int errcap);

/* Runs one SELECT statement. On success, packs the matching rows into
 * `out` (see db_engine.c's PACKED ROW FORMAT comment) and returns the row
 * count. On error returns -1 and writes a human-readable message to
 * `out` instead. */
int sqdb_query(const char *sql, char *out, int outcap);

/* The autoincrement id assigned by the most recent sqdb_exec() INSERT
 * (see db_engine.c's own comment on how the autoincrement column is
 * chosen) -- 0 if the last statement wasn't an INSERT that assigned one. */
long sqdb_insert_id(void);

#endif
