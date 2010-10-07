<?php

/* demonstrating the ability to memleak all over the place */

function doWork () {
	echo "hi there\n";
	var_dump(memory_get_usage());
	exit();
}
sleep(3);

while($i<20) {
	thread_create('doWork');
	usleep(50000);
	$i++;
}
sleep(3);

exit();