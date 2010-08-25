<?php

function threaded_client_connection ($i) {
	// make results come back in different order
	sleep((11-$i));

	$fp = stream_socket_client("tcp://127.0.0.1:9000", $errno, $errstr, 30);
	if (!$fp) {
		echo "$errstr ($errno)<br />\n";
	} else {
		fwrite($fp, "hello from thread $i\n");
		while (!feof($fp)) {
			echo fgets($fp, 1024);
		}
		fclose($fp);
	}
}

// create 10 different connections
for($i = 1; $i <= 10; $i++)
	thread_create('threaded_client_connection', $i);
?>