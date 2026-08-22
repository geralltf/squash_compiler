<!DOCTYPE html>
<html>
<body>
<h1>PHP feature test</h1>
<?php
if ( ! defined( 'GREETING' ) ) {
    define( 'GREETING', 'Hello' );
}
echo "<p>Const: ", GREETING, "</p>";
echo "<p>Defined check: ", ( defined( 'GREETING' ) ? "yes" : "no" ), "</p>";
echo "<p>Dir: ", __DIR__, "</p>";

function add_numbers( $a, $b ) {
    $sum = $a + $b;
    return $sum;
}
$result = add_numbers( 3, 4 );
echo "<p>3+4=", $result, "</p>";

function greet( $name ) {
    if ( $name == "" ) {
        return "nobody";
    }
    return GREETING . ", " . $name . "!";
}
echo "<p>", greet( "World" ), "</p>";
echo "<p>", greet( "" ), "</p>";

echo "<p>", sprintf( "%s scored %d out of %d", "Alice", 9, 10 ), "</p>";
echo "<p>", sprintf( "%2$s before %1$s", "second", "first" ), "</p>";

require_once __DIR__ . '/phpfeatures_included.php';
echo "<p>After include, shared_var=", $shared_var, "</p>";
echo "<p>included_func(5)=", included_func( 5 ), "</p>";
?>
</body>
</html>
