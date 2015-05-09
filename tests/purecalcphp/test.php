<?php

function fibonacci($number)
{
 if ($number < 2) return $number;
 return fibonacci($number - 1) + fibonacci($number - 2);
}

for($i=0;$i<100;$i++)
fibonacci(34);

?>
