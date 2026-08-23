<?php
// Standalone smoke test for the native DB engine's PHP-level glue
// builtins (__db_open/__db_exec/__db_query/__db_insert/__db_update/
// __db_delete/__db_get_var/__db_get_col/__db_insert_id/__db_last_error)
// -- run directly through php_run() via a small harness, NOT through the
// $wpdb class (that's exercised separately once wired into the real
// WordPress tree). Prints PASS/FAIL lines a harness can grep for.

__db_open("/tmp/sqdb_smoke_data");
__db_exec("CREATE TABLE t (id, name, note)");

$data = array("name" => "alice", "note" => "first");
$r = __db_insert("t", $data);
if ($r == 1) { echo "PASS insert1\n"; } else { echo "FAIL insert1 r=$r\n"; }
$id1 = __db_insert_id();
if ($id1 == 1) { echo "PASS insert_id1\n"; } else { echo "FAIL insert_id1 got=$id1\n"; }

$r = __db_insert("t", array("name" => "bob", "note" => "second"));
if ($r == 1) { echo "PASS insert2\n"; } else { echo "FAIL insert2 r=$r\n"; }

$results = __db_query("SELECT * FROM t ORDER BY name");
if (count($results) == 2) { echo "PASS query_count\n"; } else { echo "FAIL query_count got=" . count($results) . "\n"; }
if ($results[0]->name == "alice") { echo "PASS row0_name\n"; } else { echo "FAIL row0_name got={$results[0]->name}\n"; }
if ($results[1]->note == "second") { echo "PASS row1_note\n"; } else { echo "FAIL row1_note got={$results[1]->note}\n"; }

$v = __db_get_var(1, 0);
if ($v == "alice") { echo "PASS get_var\n"; } else { echo "FAIL get_var got=$v\n"; }

$col = __db_get_col(1);
if ($col[0] == "alice" && $col[1] == "bob") { echo "PASS get_col\n"; } else { echo "FAIL get_col\n"; }

$upd = __db_update("t", array("note" => "updated"), array("name" => "alice"));
if ($upd == 1) { echo "PASS update\n"; } else { echo "FAIL update r=$upd\n"; }

$check = __db_query("SELECT note FROM t WHERE name = 'alice'");
if ($check[0]->note == "updated") { echo "PASS update_verify\n"; } else { echo "FAIL update_verify got={$check[0]->note}\n"; }

$del = __db_delete("t", array("name" => "bob"));
if ($del == 1) { echo "PASS delete\n"; } else { echo "FAIL delete r=$del\n"; }

$remaining = __db_query("SELECT * FROM t");
if (count($remaining) == 1) { echo "PASS delete_verify\n"; } else { echo "FAIL delete_verify got=" . count($remaining) . "\n"; }

// A value containing a single quote and a backslash -- SQL-escaping
// round trip.
__db_insert("t", array("name" => "quo'te\\slash", "note" => "x"));
$q = __db_query("SELECT name FROM t WHERE note = 'x'");
if ($q[0]->name == "quo'te\\slash") { echo "PASS escaping\n"; } else { echo "FAIL escaping got={$q[0]->name}\n"; }

echo "DONE\n";

$p = __db_prepare("SELECT * FROM t WHERE name = %s AND id = %d", "o'brien", "42");
if ($p == "SELECT * FROM t WHERE name = 'o\\'brien' AND id = 42") { echo "PASS prepare\n"; } else { echo "FAIL prepare got=[$p]\n"; }

if (__db_is_select("  select * from t") == "1") { echo "PASS is_select_true\n"; } else { echo "FAIL is_select_true\n"; }
if (__db_is_select("DELETE FROM t") == "0") { echo "PASS is_select_false\n"; } else { echo "FAIL is_select_false\n"; }

$esc = __db_escape("a'b\\c");
if ($esc == "a\\'b\\\\c") { echo "PASS escape\n"; } else { echo "FAIL escape got=[$esc]\n"; }

echo "DONE2\n";
