<!DOCTYPE html>
<html>
<body>
<h1>SQS dynamic test page</h1>
<p>Method: <?php echo $_SERVER['REQUEST_METHOD']; ?></p>
<p>Name (GET): <?php echo $_GET['name']; ?></p>
<p>Note (POST): <?php echo $_POST['note']; ?></p>
<?php
$x = 3;
$y = 4;
$sum = $x + $y;
echo "<p>Sum: ", $sum, "</p>";
if ($_GET['name'] == "world") {
    echo "<p>Greeting: Hello, world!</p>";
} else {
    echo "<p>Greeting: (no match)</p>";
}
?>
</body>
</html>
