<?php
sleep(5);
if(!extension_loaded('threading')) {
	dl('threading.' . PHP_SHLIB_SUFFIX);
}

function print_char ($char, $times) {
	for($i = 0; $i < $times; $i++) {
		echo $char;
	}
}

echo "\nMASTER: starting threads\n";

thread_create('print_char', 'x', 2000, 50);
thread_create('print_char', 'y', 2000, 50);

echo "\nMASTER: done\n";

?>