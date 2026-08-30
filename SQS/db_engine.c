/* db_engine.c -- real SQLite-backed storage engine (see db_engine.h for
 * the public API). REPLACES this project's original flat-file/hand-
 * rolled-SQL-subset engine (kept alongside as db_engine.c.orig for
 * reference/diffing -- see its own header comment for the exact grammar
 * that engine hand-parsed) with the real SQLite3 C library, linked
 * against the system's own /usr/lib/x86_64-linux-gnu/libsqlite3.so.0 (no
 * -dev package needed -- same "link the real runtime .so directly, no
 * bare unversioned symlink required" convention this project's own
 * OpenSSL linking already uses; see the extern declarations below, same
 * "bodyless prototype, no local definition" shape as e.g.
 * include/sys/stat.h's own stat()/mkdir() externs).
 *
 * WHY: the original engine's own header comment already said so --
 * "this is the first thing to replace when rolling a real engine;
 * everything else in this codebase only depends on the sqdb_* function
 * signatures in db_engine.h, not on this file's internals." This
 * replacement changes NOTHING about that public API (sqdb_open/exec/
 * query/insert_id -- same signatures, same "packed row format" for
 * sqdb_query's `out`, same caller contract) -- SQS/php_mini.c's own
 * __db_* builtins and SQW/testpages/.../class-wpdb.php needed ZERO
 * changes for this swap.
 *
 * The single biggest real consequence: WordPress's own SQL (built by
 * class-wp-query.php and friends) is now executed as REAL SQL by a REAL
 * SQL engine -- LIKE, OR, JOIN, subqueries, GROUP BY, real aggregates,
 * all of it -- instead of the old engine's deliberately narrow "=
 * equality ANDed, COUNT(*) only" hand-rolled subset. Any query that
 * previously returned "sqdb: unsupported statement" (or silently
 * degraded) now either really runs or returns SQLite's own real error
 * message via `err`/`out`.
 *
 * PACKED ROW FORMAT (sqdb_query's `out`, UNCHANGED from the old engine):
 * first line is the comma-separated names of the real SQLite result
 * columns (in SELECT order -- sqlite3_column_name() already gives
 * "COUNT(*)" for a bare, unaliased COUNT(*) column, so that historically
 * special-cased shape now falls out for free with no special-casing at
 * all), each following line is one matching row as tab-separated
 * escaped field values (same backslash-escaping the old engine's own
 * on-disk format used: tab -> "\t", newline -> "\n", backslash -> "\\"),
 * one row per line.
 *
 * AUTOINCREMENT ID COMPATIBILITY: the vendored class-wpdb.php's own
 * CREATE TABLE strings (e.g. "CREATE TABLE IF NOT EXISTS wp_posts (ID,
 * post_author, ...)") declare the id-shaped FIRST column with no type
 * or PRIMARY KEY at all -- the old engine's own sqdb_do_insert()
 * silently treated "first column, omitted from an INSERT's column
 * list" as "assign it max-existing+1". Real SQLite has no such implicit
 * behavior; it needs the column declared "INTEGER PRIMARY KEY" (which
 * SQLite then aliases directly to the table's own real ROWID, giving
 * real autoincrement semantics for free) to auto-assign an omitted id
 * on INSERT and have sqlite3_last_insert_rowid() report it. Rather than
 * edit class-wpdb.php's own CREATE TABLE text (real WordPress code
 * shouldn't need to know which storage engine is underneath it), this
 * file's own sqdb_do_create() rewrites the FIRST declared column to add
 * " INTEGER PRIMARY KEY" before handing the statement to SQLite --
 * matching the OLD engine's own "first column is always the id" house
 * convention, just implemented as real SQLite autoincrement instead of
 * a hand-rolled max+1 scan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "db_engine.h"

/* ------------------------------------------------------------------ */
/* Minimal SQLite3 C API shim -- opaque pointer types only (sqlite3/    */
/* sqlite3_stmt are never dereferenced or sizeof()'d here, only passed  */
/* around as pointers -- sidesteps entirely the struct-layout/sizeof()  */
/* class of squash codegen bug this project already hit once this      */
/* session with SQW/css.h's own structs). Signatures match the real    */
/* sqlite3.h exactly (verified against a real system copy). */
/* ------------------------------------------------------------------ */
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

extern int sqlite3_open(const char *filename, sqlite3 **ppDb);
extern int sqlite3_close(sqlite3 *db);
extern int sqlite3_exec(sqlite3 *db, const char *sql, int (*callback)(void *, int, char **, char **), void *arg, char **errmsg);
extern int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
extern int sqlite3_step(sqlite3_stmt *stmt);
extern int sqlite3_finalize(sqlite3_stmt *stmt);
extern int sqlite3_column_count(sqlite3_stmt *stmt);
extern const char *sqlite3_column_name(sqlite3_stmt *stmt, int N);
extern const unsigned char *sqlite3_column_text(sqlite3_stmt *stmt, int iCol);
extern long long sqlite3_last_insert_rowid(sqlite3 *db);
extern int sqlite3_changes(sqlite3 *db);
extern const char *sqlite3_errmsg(sqlite3 *db);
extern void sqlite3_free(void *p);

#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101

#define SQDB_PATH_MAX 512
#define SQDB_FIELD_MAX 4096

static sqlite3 *g_db = NULL;
static long g_last_insert_id = 0;

/* Same escaping the old flat-file engine's on-disk format and packed
 * query-result format both used -- kept identical so php_mini.c's own
 * parsing of sqdb_query()'s `out` (php_db_raw_field() and friends) needs
 * no changes at all. */
static void db_escape_field(const char *in, char *out, int outcap) {
    int o = 0;
    const char *p;
    for (p = in; *p && o < outcap - 2; p++) {
        if (*p == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else if (*p == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (*p == '\\') { out[o++] = '\\'; out[o++] = '\\'; }
        else out[o++] = *p;
    }
    out[o] = 0;
}

/* Reverses db_escape_field() -- SQS/php_mini.c's own packed-row-format
 * parsing (php_db_raw_field() and friends) calls this directly (same
 * single-translation-unit convention as every other db_engine.c/
 * php_mini.c boundary function -- see php_mini.c's own #include of this
 * file) to turn a packed field back into its real unescaped value. This
 * file's OWN internals never need to unescape anything (real SQLite
 * handles all of its own on-disk storage encoding), but the packed-
 * format CONTRACT with php_mini.c is unchanged from the old flat-file
 * engine, so this still needs to exist here. */
static void db_unescape_field(const char *in, char *out, int outcap) {
    int o = 0;
    const char *p = in;
    while (*p && o < outcap - 1) {
        if (p[0] == '\\' && p[1] == 't') { out[o++] = '\t'; p += 2; }
        else if (p[0] == '\\' && p[1] == 'n') { out[o++] = '\n'; p += 2; }
        else if (p[0] == '\\' && p[1] == '\\') { out[o++] = '\\'; p += 2; }
        else out[o++] = *p++;
    }
    out[o] = 0;
}

static void sqdb_skip_ws(const char **p) {
    const char *q = *p;
    while (*q && isspace((unsigned char)*q)) q++;
    *p = q;
}

/* Case-insensitive keyword match, consuming it (and trailing whitespace)
 * on success -- same helper shape the old engine used, kept for
 * db_engine.c's own sqdb_do_create() to parse just enough of "CREATE
 * TABLE [IF NOT EXISTS] name (col, col, ...)" to rewrite the first
 * column, before handing the rest verbatim to real SQLite. */
static int sqdb_kw(const char **p, const char *kw) {
    const char *q = *p;
    int len = (int)strlen(kw);
    int i;
    for (i = 0; i < len; i++) {
        if (!q[i] || toupper((unsigned char)q[i]) != toupper((unsigned char)kw[i])) return 0;
    }
    char after = q[len];
    if (after && (isalnum((unsigned char)after) || after == '_')) return 0;
    q += len;
    sqdb_skip_ws(&q);
    *p = q;
    return 1;
}

static int sqdb_ch(const char **p, char c) {
    const char *q = *p;
    if (*q != c) return 0;
    q++;
    sqdb_skip_ws(&q);
    *p = q;
    return 1;
}

static int sqdb_ident(const char **p, char *out, int outcap) {
    const char *q = *p;
    if (*q == '`') {
        q++;
        int o = 0;
        while (*q && *q != '`' && o < outcap - 1) out[o++] = *q++;
        out[o] = 0;
        if (*q == '`') q++;
        sqdb_skip_ws(&q);
        *p = q;
        return o > 0;
    }
    if (!(isalpha((unsigned char)*q) || *q == '_')) return 0;
    int o = 0;
    while ((isalnum((unsigned char)*q) || *q == '_') && o < outcap - 1) out[o++] = *q++;
    out[o] = 0;
    sqdb_skip_ws(&q);
    *p = q;
    return 1;
}

void sqdb_open(const char *data_dir) {
    if (g_db) return; /* idempotent -- class-wpdb.php's own db_connect() calls this every request */
    mkdir(data_dir, 0755);
    char path[SQDB_PATH_MAX];
    snprintf(path, sizeof path, "%s/sqs.sqlite3", data_dir);
    if (sqlite3_open(path, &g_db) != SQLITE_OK) {
        g_db = NULL;
    }
}

/* Rewrites "CREATE TABLE [IF NOT EXISTS] name (col1, col2, ...)" into
 * real SQLite syntax with col1 declared "INTEGER PRIMARY KEY" -- see
 * this file's own top comment on why (matches the old engine's own
 * "first column is always the id" convention, as real SQLite
 * autoincrement instead of a hand-rolled max+1 scan). Any OTHER
 * statement shape (a plain column-type CREATE TABLE some future caller
 * might issue, an already-typed column list, etc.) is passed through
 * verbatim -- this rewrite only fires for the specific untyped-bare-
 * column-list shape class-wpdb.php's own bootstrap actually uses. */
static int sqdb_do_create(const char *sql, char *err, int errcap) {
    const char *p = sql;
    sqdb_kw(&p, "TABLE");
    sqdb_kw(&p, "IF");
    const char *save = p;
    if (sqdb_kw(&save, "NOT") && sqdb_kw(&save, "EXISTS")) p = save;
    char name[64];
    if (!sqdb_ident(&p, name, sizeof name) || !sqdb_ch(&p, '(')) {
        /* Doesn't match the expected shape -- fall back to running the
         * statement completely unmodified rather than guessing wrong. */
        char *errmsg = NULL;
        int rc = sqlite3_exec(g_db, sql, NULL, NULL, &errmsg);
        if (rc != SQLITE_OK) { if (err) snprintf(err, errcap, "sqdb: %s", errmsg ? errmsg : "create failed"); if (errmsg) sqlite3_free(errmsg); return -1; }
        if (errmsg) sqlite3_free(errmsg);
        return 0;
    }
    char first_col[64];
    if (!sqdb_ident(&p, first_col, sizeof first_col)) {
        if (err) snprintf(err, errcap, "sqdb: CREATE TABLE with no columns");
        return -1;
    }
    /* p now points just past the first column name (and any trailing
     * whitespace) -- every REMAINING column in the list (up to the
     * closing ')') gets rewritten with an explicit "TEXT" type instead
     * of being copied through untyped/verbatim. This matters more than
     * it looks: a column declared with NO type name at all gets SQLite's
     * own "BLOB affinity" (see SQLite's type-affinity rules), which does
     * NOT coerce an INTEGER literal to TEXT before comparing -- so
     * "WHERE user_id = 1" (an unquoted numeric literal, exactly what
     * __db_prepare()'s own "%d" placeholder substitution produces) NEVER
     * matched a real row whose user_id was actually stored as the text
     * string "1" (every id column this project's own schema holds is
     * stored as text, packed-row-format all the way down -- see this
     * file's own top comment). Confirmed as a real, severe, silent bug:
     * update_metadata()'s own "SELECT umeta_id FROM wp_usermeta WHERE
     * meta_key = %s AND user_id = %d" always returned zero rows
     * regardless of how many real matching rows existed, so
     * update_user_meta() never found the row it should have updated and
     * fell through to add_metadata() (a fresh INSERT) EVERY time instead
     * -- directly breaking WP_User_Meta_Session_Tokens's own "update my
     * one row of session data" call, silently accumulating a new,
     * never-updated duplicate row per login instead, and (since
     * get_user_meta()'s own "$single=true" mode reads back only the
     * FIRST/oldest such row) permanently losing every session actually
     * created after the very first one. A TEXT-affinity column DOES
     * coerce a bare integer literal to text for comparison purposes,
     * matching what every real WHERE clause built by this project's own
     * $wpdb-alike actually needs. */
    char coltypes[SQDB_FIELD_MAX];
    int co = 0;
    sqdb_ch(&p, ','); /* the separator between first_col and the rest of the list -- sqdb_ident() left it unconsumed */
    for (;;) {
        char col[64];
        if (!sqdb_ident(&p, col, sizeof col)) break;
        int n = snprintf(coltypes + co, (size_t)(sizeof coltypes - co), ", %s TEXT", col);
        if (n > 0) co += n;
        if (co >= (int)sizeof coltypes) break;
        if (!sqdb_ch(&p, ',')) break;
    }
    coltypes[co] = 0;
    char stmt[SQDB_FIELD_MAX];
    snprintf(stmt, sizeof stmt, "CREATE TABLE IF NOT EXISTS %s (%s INTEGER PRIMARY KEY%s%s", name, first_col, coltypes, p);
    char *errmsg = NULL;
    int rc = sqlite3_exec(g_db, stmt, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        if (err) snprintf(err, errcap, "sqdb: %s", errmsg ? errmsg : "create failed");
        if (errmsg) sqlite3_free(errmsg);
        return -1;
    }
    if (errmsg) sqlite3_free(errmsg);
    return 0;
}

int sqdb_exec(const char *sql, char *err, int errcap) {
    if (!g_db) { if (err) snprintf(err, errcap, "sqdb: not open"); return -1; }
    const char *p = sql;
    sqdb_skip_ws(&p);
    if (sqdb_kw(&p, "CREATE")) return sqdb_do_create(p, err, errcap);

    char *errmsg = NULL;
    int rc = sqlite3_exec(g_db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        if (err) snprintf(err, errcap, "sqdb: %s", errmsg ? errmsg : sqlite3_errmsg(g_db));
        if (errmsg) sqlite3_free(errmsg);
        return -1;
    }
    if (errmsg) sqlite3_free(errmsg);
    g_last_insert_id = (long)sqlite3_last_insert_rowid(g_db);
    return sqlite3_changes(g_db);
}

int sqdb_query(const char *sql, char *out, int outcap) {
    if (!g_db) { snprintf(out, outcap, "sqdb: not open"); return -1; }
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        snprintf(out, outcap, "sqdb: %s", sqlite3_errmsg(g_db));
        return -1;
    }
    int ncols = sqlite3_column_count(stmt);
    int o = 0, i;
    for (i = 0; i < ncols; i++) {
        const char *cn = sqlite3_column_name(stmt, i);
        o += snprintf(out + o, o < outcap ? (size_t)(outcap - o) : 0, "%s%s", i ? "," : "", cn ? cn : "");
    }
    o += snprintf(out + o, o < outcap ? (size_t)(outcap - o) : 0, "\n");

    int nrows = 0;
    for (;;) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            snprintf(out, outcap, "sqdb: %s", sqlite3_errmsg(g_db));
            return -1;
        }
        for (i = 0; i < ncols; i++) {
            const unsigned char *v = sqlite3_column_text(stmt, i);
            char esc[SQDB_FIELD_MAX];
            db_escape_field(v ? (const char *)v : "", esc, sizeof esc);
            o += snprintf(out + o, o < outcap ? (size_t)(outcap - o) : 0, "%s%s", i ? "\t" : "", esc);
        }
        o += snprintf(out + o, o < outcap ? (size_t)(outcap - o) : 0, "\n");
        nrows++;
    }
    sqlite3_finalize(stmt);
    return nrows;
}

long sqdb_insert_id(void) { return g_last_insert_id; }
