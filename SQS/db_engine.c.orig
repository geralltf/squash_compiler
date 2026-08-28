/* db_engine.c -- tiny flat-file SQL-subset storage engine (see
 * db_engine.h for the public API and the exact SQL grammar supported).
 *
 * Deliberately decoupled from SQS/php_mini.c: this file knows nothing
 * about PhpObject/PhpKVArray/PhpState -- it only ever sees and returns
 * plain C strings. The PHP-side glue (the __db_* builtins added to
 * php_call_function() in php_mini.c) is what turns a SELECT's packed
 * text result into real PHP row objects. That split is deliberate: it's
 * what lets this file be swapped out later for a real storage engine
 * without touching the interpreter at all.
 *
 * STORAGE FORMAT: one flat text file per table, "<data_dir>/<table>.tbl".
 * Line 1: comma-separated column names. Each following line: one row,
 * tab-separated field values, backslash-escaped (a literal tab becomes
 * "\t", a literal newline becomes "\n", a literal backslash becomes
 * "\\", so every row is exactly one line). Every write (INSERT/UPDATE/
 * DELETE/CREATE) loads the whole file, mutates it in memory, and
 * rewrites it from scratch -- no indexing, no WAL, no locking. This is
 * the first thing to replace when rolling a real engine; everything
 * else in this codebase only depends on the sqdb_* function signatures
 * in db_engine.h, not on this file's internals.
 *
 * PACKED ROW FORMAT (sqdb_query's `out`): first line is the
 * comma-separated names of the SELECTed columns (in SELECT order), each
 * following line is one matching row as tab-separated escaped field
 * values (same escaping as the on-disk format), one row per line.
 *
 * SQL GRAMMAR SUPPORTED (case-insensitive keywords; identifiers may
 * optionally be backtick-quoted; string literals are single-quoted with
 * '' or \' as an escaped quote):
 *   CREATE TABLE [IF NOT EXISTS] name (col [ignored...], col [ignored...], ...)
 *   INSERT INTO name (col, ...) VALUES (val, ...)
 *   SELECT (* | COUNT(*) | col [, col ...]) FROM name
 *        [WHERE col = val [AND col = val ...]]
 *        [ORDER BY col [ASC|DESC]] [LIMIT n]
 *   UPDATE name SET col = val [, col = val ...] [WHERE col = val [AND ...]]
 *   DELETE FROM name [WHERE col = val [AND ...]]
 * WHERE only supports "=" equality, ANDed -- no OR/LIKE/IN/JOIN/
 * subqueries/GROUP BY. COUNT(*) is the one aggregate supported (it's the
 * single most common one real WordPress issues at boot, e.g.
 * is_blog_installed()'s "SELECT COUNT(*) FROM $wpdb->tables"-shaped
 * queries) -- no other aggregate functions. This is the explicit "simple
 * approach first" boundary the caller was told about; anything past this
 * grammar returns a "sqdb: unsupported ..." error string rather than
 * silently doing the wrong thing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "db_engine.h"

#define SQDB_MAX_COLS 32
#define SQDB_FIELD_MAX 1024
#define SQDB_PATH_MAX 512
#define SQDB_IDENT_MAX 128
#define SQDB_ROW_GROW 16

static char g_data_dir[SQDB_PATH_MAX];
static long g_last_insert_id = 0;

typedef struct {
    char *fields[SQDB_MAX_COLS]; /* malloc'd, unescaped; only [0..ncols) valid */
} DBRow;

typedef struct {
    char name[64];
    char cols[SQDB_MAX_COLS][64];
    int ncols;
    DBRow *rows;
    int nrows;
    int rowcap;
} DBTable;

/* ------------------------------------------------------------------ */
/* escaping                                                            */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* table load/save/free                                                */
/* ------------------------------------------------------------------ */

static void db_path(const char *table, char *out, int outcap) {
    snprintf(out, outcap, "%s/%s.tbl", g_data_dir, table);
}

static void db_table_free(DBTable *t) {
    int i, c;
    if (!t) return;
    for (i = 0; i < t->nrows; i++) for (c = 0; c < t->ncols; c++) free(t->rows[i].fields[c]);
    free(t->rows);
    free(t);
}

static void db_table_add_row(DBTable *t) {
    if (t->nrows >= t->rowcap) {
        int newcap = t->rowcap + SQDB_ROW_GROW;
        DBRow *nr = (DBRow *)realloc(t->rows, (size_t)newcap * sizeof(DBRow));
        if (!nr) return;
        t->rows = nr;
        t->rowcap = newcap;
    }
    memset(&t->rows[t->nrows], 0, sizeof(DBRow));
    t->nrows++;
}

/* Reads one line (up to '\n' or EOF) into buf; returns 0 at EOF with
 * nothing read, 1 otherwise. Strips the trailing '\n'/'\r'. */
static int db_readline(FILE *f, char *buf, int cap) {
    if (!fgets(buf, cap, f)) return 0;
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
    return 1;
}

static DBTable *db_table_load(const char *table) {
    char path[SQDB_PATH_MAX];
    db_path(table, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    DBTable *t = (DBTable *)calloc(1, sizeof(DBTable));
    strncpy(t->name, table, sizeof t->name - 1);

    char line[SQDB_MAX_COLS * SQDB_FIELD_MAX];
    if (!db_readline(f, line, sizeof line)) { fclose(f); db_table_free(t); return NULL; }
    char *save = NULL;
    char *tok = strtok_r(line, ",", &save);
    while (tok && t->ncols < SQDB_MAX_COLS) {
        strncpy(t->cols[t->ncols], tok, sizeof t->cols[t->ncols] - 1);
        t->ncols++;
        tok = strtok_r(NULL, ",", &save);
    }

    while (db_readline(f, line, sizeof line)) {
        db_table_add_row(t);
        DBRow *row = &t->rows[t->nrows - 1];
        char *rsave = NULL;
        char *rtok = strtok_r(line, "\t", &rsave);
        int c = 0;
        while (rtok && c < t->ncols) {
            char unesc[SQDB_FIELD_MAX];
            db_unescape_field(rtok, unesc, sizeof unesc);
            row->fields[c] = strdup(unesc);
            c++;
            rtok = strtok_r(NULL, "\t", &rsave);
        }
        for (; c < t->ncols; c++) row->fields[c] = strdup("");
    }
    fclose(f);
    return t;
}

static int db_table_save(DBTable *t) {
    char path[SQDB_PATH_MAX];
    db_path(t->name, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int i, c;
    for (c = 0; c < t->ncols; c++) fprintf(f, "%s%s", c ? "," : "", t->cols[c]);
    fputc('\n', f);
    for (i = 0; i < t->nrows; i++) {
        for (c = 0; c < t->ncols; c++) {
            char esc[SQDB_FIELD_MAX];
            db_escape_field(t->rows[i].fields[c] ? t->rows[i].fields[c] : "", esc, sizeof esc);
            fprintf(f, "%s%s", c ? "\t" : "", esc);
        }
        fputc('\n', f);
    }
    fclose(f);
    return 1;
}

static int db_table_col_index(DBTable *t, const char *name) {
    int i;
    for (i = 0; i < t->ncols; i++) if (strcasecmp(t->cols[i], name) == 0) return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* tiny SQL tokenizer                                                  */
/* ------------------------------------------------------------------ */

static void sqdb_skip_ws(const char **p) { while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') (*p)++; }

/* Case-insensitive whole-keyword match at *p (must be followed by a
 * non-identifier character). Advances *p and trims trailing whitespace
 * on success; leaves *p untouched on failure. */
static int sqdb_kw(const char **p, const char *kw) {
    const char *q = *p;
    int n = (int)strlen(kw);
    if (strncasecmp(q, kw, n) != 0) return 0;
    char after = q[n];
    if (isalnum((unsigned char)after) || after == '_') return 0;
    *p = q + n;
    sqdb_skip_ws(p);
    return 1;
}

/* Reads a bare identifier, optionally backtick-quoted. */
static int sqdb_ident(const char **p, char *out, int outcap) {
    const char *q = *p;
    int o = 0;
    int quoted = (*q == '`');
    if (quoted) q++;
    while ((isalnum((unsigned char)*q) || *q == '_') && o < outcap - 1) out[o++] = *q++;
    out[o] = 0;
    if (o == 0) return 0;
    if (quoted) { if (*q == '`') q++; }
    *p = q;
    sqdb_skip_ws(p);
    return 1;
}

/* Reads one value: a single-quoted string literal (with '' / \' as an
 * escaped quote) or a bare token (number/NULL/word) up to the next
 * delimiter. Either way `out` receives the literal value with quoting
 * removed. */
static int sqdb_value(const char **p, char *out, int outcap) {
    const char *q = *p;
    int o = 0;
    if (*q == '\'') {
        q++;
        while (*q && o < outcap - 1) {
            if (*q == '\\' && q[1]) { out[o++] = q[1]; q += 2; }
            else if (*q == '\'' && q[1] == '\'') { out[o++] = '\''; q += 2; }
            else if (*q == '\'') { q++; break; }
            else out[o++] = *q++;
        }
        out[o] = 0;
        *p = q;
        sqdb_skip_ws(p);
        return 1;
    }
    while (*q && *q != ',' && *q != ')' && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r' && o < outcap - 1) out[o++] = *q++;
    out[o] = 0;
    if (o == 0) return 0;
    *p = q;
    sqdb_skip_ws(p);
    return 1;
}

static int sqdb_ch(const char **p, char c) {
    const char *q = *p;
    if (*q != c) return 0;
    *p = q + 1;
    sqdb_skip_ws(p);
    return 1;
}

/* ------------------------------------------------------------------ */
/* WHERE-clause matching                                               */
/* ------------------------------------------------------------------ */

typedef struct { char col[SQDB_IDENT_MAX]; char val[SQDB_FIELD_MAX]; } SqdbCond;

/* Parses "col = val [AND col = val ...]" (the caller must already have
 * consumed the WHERE keyword). Returns the condition count, or -1 on a
 * syntax error. */
static int sqdb_parse_where(const char **p, SqdbCond *conds, int maxconds) {
    int n = 0;
    for (;;) {
        if (n >= maxconds) return -1;
        if (!sqdb_ident(p, conds[n].col, sizeof conds[n].col)) return -1;
        if (!sqdb_ch(p, '=')) return -1;
        if (!sqdb_value(p, conds[n].val, sizeof conds[n].val)) return -1;
        n++;
        if (sqdb_kw(p, "AND")) continue;
        break;
    }
    return n;
}

static int sqdb_values_equal(const char *a, const char *b) {
    char *enda, *endb;
    long la = strtol(a, &enda, 10), lb = strtol(b, &endb, 10);
    if (*enda == 0 && *endb == 0 && a[0] && b[0]) return la == lb;
    return strcmp(a, b) == 0;
}

static int sqdb_row_matches(DBTable *t, DBRow *row, SqdbCond *conds, int nconds) {
    int i;
    for (i = 0; i < nconds; i++) {
        int ci = db_table_col_index(t, conds[i].col);
        if (ci < 0) return 0;
        const char *have = row->fields[ci] ? row->fields[ci] : "";
        if (!sqdb_values_equal(have, conds[i].val)) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

void sqdb_open(const char *data_dir) {
    strncpy(g_data_dir, data_dir, sizeof g_data_dir - 1);
    g_data_dir[sizeof g_data_dir - 1] = 0;
    mkdir(g_data_dir, 0755);
}

long sqdb_insert_id(void) { return g_last_insert_id; }

static int sqdb_do_create(const char *p, char *err, int errcap) {
    sqdb_kw(&p, "IF"); /* best-effort: "IF NOT EXISTS" is accepted and ignored below */
    const char *save = p;
    if (sqdb_kw(&save, "NOT") && sqdb_kw(&save, "EXISTS")) p = save;
    char name[64];
    if (!sqdb_ident(&p, name, sizeof name) || !sqdb_ch(&p, '(')) {
        if (err) snprintf(err, errcap, "sqdb: malformed CREATE TABLE");
        return -1;
    }
    char path[SQDB_PATH_MAX];
    db_path(name, path, sizeof path);
    struct stat sb;
    if (stat(path, &sb) == 0) return 0; /* already exists: no-op success */

    DBTable t; memset(&t, 0, sizeof t);
    strncpy(t.name, name, sizeof t.name - 1);
    while (t.ncols < SQDB_MAX_COLS) {
        char colname[64];
        if (!sqdb_ident(&p, colname, sizeof colname)) break;
        strncpy(t.cols[t.ncols], colname, sizeof t.cols[t.ncols] - 1);
        t.ncols++;
        /* skip any trailing type/constraint tokens up to the next ',' or ')' */
        while (*p && *p != ',' && *p != ')') p++;
        if (*p == ',') { p++; sqdb_skip_ws(&p); continue; }
        break;
    }
    if (t.ncols == 0) {
        if (err) snprintf(err, errcap, "sqdb: CREATE TABLE with no columns");
        return -1;
    }
    if (!db_table_save(&t)) {
        if (err) snprintf(err, errcap, "sqdb: could not write table file for '%s'", name);
        return -1;
    }
    return 0;
}

static int sqdb_do_insert(const char *p, char *err, int errcap) {
    if (!sqdb_kw(&p, "INTO")) { if (err) snprintf(err, errcap, "sqdb: expected INTO"); return -1; }
    char name[64];
    if (!sqdb_ident(&p, name, sizeof name)) { if (err) snprintf(err, errcap, "sqdb: malformed INSERT"); return -1; }
    DBTable *t = db_table_load(name);
    if (!t) { if (err) snprintf(err, errcap, "sqdb: no such table '%s'", name); return -1; }

    char cols[SQDB_MAX_COLS][64];
    int ncols = 0;
    if (sqdb_ch(&p, '(')) {
        while (ncols < SQDB_MAX_COLS) {
            if (!sqdb_ident(&p, cols[ncols], sizeof cols[ncols])) break;
            ncols++;
            if (sqdb_ch(&p, ',')) continue;
            break;
        }
        sqdb_ch(&p, ')');
    }
    if (!sqdb_kw(&p, "VALUES") || !sqdb_ch(&p, '(')) {
        db_table_free(t);
        if (err) snprintf(err, errcap, "sqdb: expected VALUES (...)");
        return -1;
    }
    char vals[SQDB_MAX_COLS][SQDB_FIELD_MAX];
    int nvals = 0;
    while (nvals < SQDB_MAX_COLS) {
        if (!sqdb_value(&p, vals[nvals], sizeof vals[nvals])) break;
        nvals++;
        if (sqdb_ch(&p, ',')) continue;
        break;
    }
    sqdb_ch(&p, ')');

    if (ncols == 0) {
        /* "INSERT INTO t VALUES (...)" with no column list: positional,
         * one value per table column in order. */
        ncols = t->ncols;
        int i;
        for (i = 0; i < ncols && i < SQDB_MAX_COLS; i++) strncpy(cols[i], t->cols[i], sizeof cols[i] - 1);
    }
    if (ncols != nvals) {
        db_table_free(t);
        if (err) snprintf(err, errcap, "sqdb: column/value count mismatch");
        return -1;
    }

    db_table_add_row(t);
    DBRow *row = &t->rows[t->nrows - 1];
    int i;
    for (i = 0; i < t->ncols; i++) row->fields[i] = strdup("");
    int have_pk = 0;
    for (i = 0; i < ncols; i++) {
        int ci = db_table_col_index(t, cols[i]);
        if (ci < 0) continue;
        free(row->fields[ci]);
        row->fields[ci] = strdup(vals[i]);
        if (ci == 0) have_pk = 1;
    }
    if (!have_pk && t->ncols > 0) {
        long maxid = 0, r;
        for (r = 0; r < t->nrows - 1; r++) {
            char *endp;
            long v = strtol(t->rows[r].fields[0] ? t->rows[r].fields[0] : "0", &endp, 10);
            if (*endp == 0 && v > maxid) maxid = v;
        }
        char idbuf[32];
        snprintf(idbuf, sizeof idbuf, "%ld", maxid + 1);
        free(row->fields[0]);
        row->fields[0] = strdup(idbuf);
        g_last_insert_id = maxid + 1;
    } else if (t->ncols > 0) {
        g_last_insert_id = strtol(row->fields[0] ? row->fields[0] : "0", NULL, 10);
    }

    int ok = db_table_save(t);
    db_table_free(t);
    if (!ok) { if (err) snprintf(err, errcap, "sqdb: could not write table file"); return -1; }
    return 1;
}

static int sqdb_do_update(const char *p, char *err, int errcap) {
    char name[64];
    if (!sqdb_ident(&p, name, sizeof name) || !sqdb_kw(&p, "SET")) {
        if (err) snprintf(err, errcap, "sqdb: malformed UPDATE");
        return -1;
    }
    DBTable *t = db_table_load(name);
    if (!t) { if (err) snprintf(err, errcap, "sqdb: no such table '%s'", name); return -1; }

    char setcols[SQDB_MAX_COLS][64], setvals[SQDB_MAX_COLS][SQDB_FIELD_MAX];
    int nset = 0;
    while (nset < SQDB_MAX_COLS) {
        if (!sqdb_ident(&p, setcols[nset], sizeof setcols[nset])) break;
        if (!sqdb_ch(&p, '=')) break;
        if (!sqdb_value(&p, setvals[nset], sizeof setvals[nset])) break;
        nset++;
        if (sqdb_ch(&p, ',')) continue;
        break;
    }

    SqdbCond conds[16];
    int nconds = 0;
    if (sqdb_kw(&p, "WHERE")) {
        nconds = sqdb_parse_where(&p, conds, 16);
        if (nconds < 0) { db_table_free(t); if (err) snprintf(err, errcap, "sqdb: malformed WHERE"); return -1; }
    }

    int count = 0, i, r;
    for (r = 0; r < t->nrows; r++) {
        if (!sqdb_row_matches(t, &t->rows[r], conds, nconds)) continue;
        for (i = 0; i < nset; i++) {
            int ci = db_table_col_index(t, setcols[i]);
            if (ci < 0) continue;
            free(t->rows[r].fields[ci]);
            t->rows[r].fields[ci] = strdup(setvals[i]);
        }
        count++;
    }
    int ok = db_table_save(t);
    db_table_free(t);
    if (!ok) { if (err) snprintf(err, errcap, "sqdb: could not write table file"); return -1; }
    return count;
}

static int sqdb_do_delete(const char *p, char *err, int errcap) {
    if (!sqdb_kw(&p, "FROM")) { if (err) snprintf(err, errcap, "sqdb: expected FROM"); return -1; }
    char name[64];
    if (!sqdb_ident(&p, name, sizeof name)) { if (err) snprintf(err, errcap, "sqdb: malformed DELETE"); return -1; }
    DBTable *t = db_table_load(name);
    if (!t) { if (err) snprintf(err, errcap, "sqdb: no such table '%s'", name); return -1; }

    SqdbCond conds[16];
    int nconds = 0;
    if (sqdb_kw(&p, "WHERE")) {
        nconds = sqdb_parse_where(&p, conds, 16);
        if (nconds < 0) { db_table_free(t); if (err) snprintf(err, errcap, "sqdb: malformed WHERE"); return -1; }
    }

    int count = 0, w = 0, r, c;
    for (r = 0; r < t->nrows; r++) {
        if (sqdb_row_matches(t, &t->rows[r], conds, nconds)) {
            for (c = 0; c < t->ncols; c++) free(t->rows[r].fields[c]);
            count++;
            continue;
        }
        if (w != r) {
            /* NOT "t->rows[w] = t->rows[r];" -- a real squash codegen bug
             * (confirmed via a minimal repro: a whole-struct assignment
             * through a POINTER-typed struct member indexed on both sides,
             * "t->rows[w] = t->rows[r]", only copies the struct's first 8
             * bytes/one field, leaving the rest of the destination at its
             * old value; the same assignment on a plain local array
             * variable, "rows[w] = rows[r]", is unaffected). Copying each
             * field explicitly sidesteps it and is just as correct. */
            for (c = 0; c < t->ncols; c++) t->rows[w].fields[c] = t->rows[r].fields[c];
        }
        w++;
    }
    t->nrows = w;
    int ok = db_table_save(t);
    db_table_free(t);
    if (!ok) { if (err) snprintf(err, errcap, "sqdb: could not write table file"); return -1; }
    return count;
}

int sqdb_exec(const char *sql, char *err, int errcap) {
    const char *p = sql;
    sqdb_skip_ws(&p);
    if (sqdb_kw(&p, "CREATE") && sqdb_kw(&p, "TABLE")) return sqdb_do_create(p, err, errcap);
    if (sqdb_kw(&p, "INSERT")) return sqdb_do_insert(p, err, errcap);
    if (sqdb_kw(&p, "UPDATE")) return sqdb_do_update(p, err, errcap);
    if (sqdb_kw(&p, "DELETE")) return sqdb_do_delete(p, err, errcap);
    if (err) snprintf(err, errcap, "sqdb: unsupported statement");
    return -1;
}

int sqdb_query(const char *sql, char *out, int outcap) {
    const char *p = sql;
    sqdb_skip_ws(&p);
    if (!sqdb_kw(&p, "SELECT")) { snprintf(out, outcap, "sqdb: unsupported statement"); return -1; }

    /* COUNT(*) -- the one aggregate this engine supports (see the file
     * header comment for why). Everything after it up to FROM/alias is
     * ignored. */
    int is_count = 0;
    const char *save = p;
    if (sqdb_kw(&save, "COUNT") && sqdb_ch(&save, '(') && sqdb_ch(&save, '*') && sqdb_ch(&save, ')')) {
        is_count = 1;
        p = save;
        if (sqdb_kw(&p, "AS")) {
            char alias[64];
            sqdb_ident(&p, alias, sizeof alias); /* discard optional alias */
        }
    }

    char selcols[SQDB_MAX_COLS][64];
    int nselcols = 0;
    int star = 0;
    if (!is_count) {
        if (sqdb_ch(&p, '*')) {
            star = 1;
        } else {
            while (nselcols < SQDB_MAX_COLS) {
                if (!sqdb_ident(&p, selcols[nselcols], sizeof selcols[nselcols])) break;
                nselcols++;
                if (sqdb_ch(&p, ',')) continue;
                break;
            }
        }
    }

    if (!sqdb_kw(&p, "FROM")) { snprintf(out, outcap, "sqdb: expected FROM"); return -1; }
    char name[64];
    if (!sqdb_ident(&p, name, sizeof name)) { snprintf(out, outcap, "sqdb: malformed SELECT"); return -1; }
    DBTable *t = db_table_load(name);
    if (!t) { snprintf(out, outcap, "sqdb: no such table '%s'", name); return -1; }

    SqdbCond conds[16];
    int nconds = 0;
    if (sqdb_kw(&p, "WHERE")) {
        nconds = sqdb_parse_where(&p, conds, 16);
        if (nconds < 0) { db_table_free(t); snprintf(out, outcap, "sqdb: malformed WHERE"); return -1; }
    }

    if (is_count) {
        int cnt = 0, r;
        for (r = 0; r < t->nrows; r++) if (sqdb_row_matches(t, &t->rows[r], conds, nconds)) cnt++;
        snprintf(out, outcap, "COUNT(*)\n%d\n", cnt);
        db_table_free(t);
        return 1;
    }

    if (star) {
        nselcols = t->ncols;
        int i;
        for (i = 0; i < nselcols; i++) strncpy(selcols[i], t->cols[i], sizeof selcols[i] - 1);
    }
    int selidx[SQDB_MAX_COLS];
    int i;
    for (i = 0; i < nselcols; i++) selidx[i] = db_table_col_index(t, selcols[i]);

    /* Gather matching row indices, then optionally sort/limit. */
    int *matched = (int *)malloc(sizeof(int) * (t->nrows > 0 ? (size_t)t->nrows : 1));
    int nmatched = 0, r;
    for (r = 0; r < t->nrows; r++) if (sqdb_row_matches(t, &t->rows[r], conds, nconds)) matched[nmatched++] = r;

    char orderby[64] = {0};
    int desc = 0;
    if (sqdb_kw(&p, "ORDER")) {
        sqdb_kw(&p, "BY");
        sqdb_ident(&p, orderby, sizeof orderby);
        if (sqdb_kw(&p, "DESC")) desc = 1; else sqdb_kw(&p, "ASC");
    }
    if (orderby[0]) {
        int oci = db_table_col_index(t, orderby);
        if (oci >= 0) {
            int a, b;
            for (a = 0; a < nmatched; a++) for (b = a + 1; b < nmatched; b++) {
                const char *va = t->rows[matched[a]].fields[oci];
                const char *vb = t->rows[matched[b]].fields[oci];
                int cmp = strcmp(va ? va : "", vb ? vb : "");
                if ((!desc && cmp > 0) || (desc && cmp < 0)) { int tmp = matched[a]; matched[a] = matched[b]; matched[b] = tmp; }
            }
        }
    }

    int limit = -1;
    if (sqdb_kw(&p, "LIMIT")) {
        char lb[32];
        if (sqdb_value(&p, lb, sizeof lb)) limit = atoi(lb);
    }
    if (limit >= 0 && nmatched > limit) nmatched = limit;

    int o = 0;
    for (i = 0; i < nselcols; i++) o += snprintf(out + o, o < outcap ? (size_t)(outcap - o) : 0, "%s%s", i ? "," : "", selcols[i]);
    o += snprintf(out + o, o < outcap ? (size_t)(outcap - o) : 0, "\n");
    for (r = 0; r < nmatched; r++) {
        DBRow *row = &t->rows[matched[r]];
        for (i = 0; i < nselcols; i++) {
            char esc[SQDB_FIELD_MAX];
            const char *v = (selidx[i] >= 0 && row->fields[selidx[i]]) ? row->fields[selidx[i]] : "";
            db_escape_field(v, esc, sizeof esc);
            o += snprintf(out + o, o < outcap ? (size_t)(outcap - o) : 0, "%s%s", i ? "\t" : "", esc);
        }
        o += snprintf(out + o, o < outcap ? (size_t)(outcap - o) : 0, "\n");
    }

    free(matched);
    db_table_free(t);
    return nmatched;
}
