#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* #include the .c directly (not just db_engine.h) -- squash can't take
 * multiple bare .c sources on one command line, and linking a separately
 * -c-compiled db_engine.sqo hits a real, pre-existing cross-.sqo symbol
 * resolution bug (see db_engine.sqo's own Makefile.SQS.linux comment) --
 * same single-translation-unit convention SQW/SDL3 already use for the
 * identical bug, and what SQS/php_mini.c itself does for the real build. */
#include "../db_engine.c"

static int nfail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); nfail++; } } while (0)

int main(void) {
    system("rm -rf /tmp/sqdb_test_data");
    sqdb_open("/tmp/sqdb_test_data");

    char err[256], out[8192];

    CHECK(sqdb_exec("CREATE TABLE wp_options (option_id, option_name, option_value)", err, sizeof err) == 0, "create table");
    CHECK(sqdb_exec("CREATE TABLE IF NOT EXISTS wp_options (option_id, option_name, option_value)", err, sizeof err) == 0, "create table idempotent");

    CHECK(sqdb_exec("INSERT INTO wp_options (option_name, option_value) VALUES ('siteurl', 'http://example.test')", err, sizeof err) == 1, "insert 1");
    CHECK(sqdb_insert_id() == 1, "insert_id 1");
    CHECK(sqdb_exec("INSERT INTO wp_options (option_name, option_value) VALUES ('blogname', 'My Site')", err, sizeof err) == 1, "insert 2");
    CHECK(sqdb_insert_id() == 2, "insert_id 2");
    CHECK(sqdb_exec("INSERT INTO wp_options (option_name, option_value) VALUES ('db_version', '12345')", err, sizeof err) == 1, "insert 3");

    int n = sqdb_query("SELECT option_value FROM wp_options WHERE option_name = 'siteurl'", out, sizeof out);
    CHECK(n == 1, "select siteurl count");
    CHECK(strstr(out, "http://example.test") != NULL, "select siteurl value");

    n = sqdb_query("SELECT * FROM wp_options ORDER BY option_name LIMIT 2", out, sizeof out);
    CHECK(n == 2, "select all limit 2");

    n = sqdb_query("SELECT COUNT(*) FROM wp_options", out, sizeof out);
    CHECK(n == 1, "count returns 1 row");
    CHECK(strstr(out, "3") != NULL, "count value is 3");

    CHECK(sqdb_exec("UPDATE wp_options SET option_value = 'http://updated.test' WHERE option_name = 'siteurl'", err, sizeof err) == 1, "update 1 row");
    n = sqdb_query("SELECT option_value FROM wp_options WHERE option_name = 'siteurl'", out, sizeof out);
    CHECK(strstr(out, "http://updated.test") != NULL, "update took effect");

    CHECK(sqdb_exec("DELETE FROM wp_options WHERE option_name = 'blogname'", err, sizeof err) == 1, "delete 1 row");
    n = sqdb_query("SELECT * FROM wp_options", out, sizeof out);
    CHECK(n == 2, "2 rows remain after delete");

    /* value with an embedded tab and backslash round-trips */
    CHECK(sqdb_exec("INSERT INTO wp_options (option_name, option_value) VALUES ('weird', 'a\\tb\\\\c')", err, sizeof err) == 1, "insert escaped-looking literal");
    n = sqdb_query("SELECT option_value FROM wp_options WHERE option_name = 'weird'", out, sizeof out);
    CHECK(n == 1, "select weird row");

    /* re-open (simulating a process restart) and confirm persistence */
    sqdb_open("/tmp/sqdb_test_data");
    n = sqdb_query("SELECT * FROM wp_options", out, sizeof out);
    CHECK(n == 3, "data persisted across reopen");

    n = sqdb_exec("SELECT * FROM wp_options", err, sizeof err);
    CHECK(n == -1, "SELECT rejected by sqdb_exec");
    n = sqdb_query("BOGUS SQL HERE", out, sizeof out);
    CHECK(n == -1, "garbage SQL rejected");

    if (nfail == 0) { printf("ALL DB ENGINE TESTS PASSED\n"); return 0; }
    printf("%d TEST(S) FAILED\n", nfail);
    return 1;
}
