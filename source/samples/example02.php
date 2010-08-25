<?php
// Set time limit to indefinite execution 
set_time_limit (0); 

$csock = array();

function echoing($fd) {
	echo "sleeping for 1 second to demo threading capabilities\n";
	sleep(1);
	
	fwrite($fd,"what you sent: ".fgets($fd, 1024));

	// cleanup, we don't want excess threads floating around.
	fclose($fd);
	unset($fd);
	echo "done sending response\n";
	exit();
}

$ssock = stream_socket_server('tcp://127.0.0.1:9000');
assert($ssock !== false);
  
// cool infinite loop
for ($i = 0;;$i++) {
	// sockets are just references in the thread, not copies
	$csock[$i] = stream_socket_accept($ssock, -1);

	echo "caught connection, creating thread!\n";
	if($csock[$i]) {
		//var_dump($csock);
		$threads[] = thread_create('echoing', $csock[$i]);
		echo "done creating thread.\n";
	}
	else {
		trigger_error('Invalid csock', E_USER_WARNING);
	}
	
	foreach($threads as $thread) {
		// this doesn't even work! i hope it will in the future. however a better
		// solution would be for threads to clean themselves up.
		thread_cleanup($thread);
	}
	//thread_clean_finished();
}

?>