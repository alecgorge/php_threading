<?php
if(!extension_loaded('threading')) {
	dl('threading.' . PHP_SHLIB_SUFFIX);
}

function print_char ($args) {
	list($char, $times, $usleep) = $args;
	for($i = 0; $i < $times; $i++) {
		echo "$i:$char\n";
		sleep(1);
	}
}

echo "\nMASTER: starting threads\n";

thread_create('print_char', array('x', 10, 50));
thread_create('print_char', array('y', 10, 50));

echo "\nMASTER: done\n";

?>