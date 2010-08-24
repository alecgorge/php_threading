<?php
error_reporting(E_ALL);

$sites = array(
	'http://example.com/',
	'http://example.net/',
	'http://example.org/',
);

function get_url_threaded ($url) {
    $defaults = array( 
        CURLOPT_URL => $url, 
        CURLOPT_HEADER => 0, 
        CURLOPT_RETURNTRANSFER => TRUE, 
        CURLOPT_TIMEOUT => 10 
    ); 
    
    $ch = curl_init(); 
    curl_setopt_array($ch, $defaults); 
    if(!$data = curl_exec($ch)) { 
        trigger_error(curl_error($ch)); 
    } 
    curl_close($ch);
	
	// sleep a little to make sure the demo loop works, remove for production code
	sleep(3);
	echo "$url download finished! Source code:\n\n".$data."\n--------------------------------\n";
}

function progress ($ch) {
	// infinite loop
	for(;;) {
		echo "still downloadin'...\n";
		
		// the second argument to thread_message_queue_poll is a timeout
		// if it times out, it returns the string (not the constant) PHP_THREAD_POLL_TIMEOUT
		// this function is blocking (it waits until a message is recieved or timeout triggered)
		$var = thread_message_queue_poll($ch, 500);
		
		if($var == 'done') return;
	}
}

foreach($sites as $site) {
	echo "Starting download of $site!\n";
	$threads[] = thread_create("get_url_threaded", $site);
}

// communication channel
$ch = thread_message_queue_create();

// progress loop
$progress = thread_create('progress', $ch);

// wait for all threads to finish
foreach($threads as $thread) {
	thread_join($thread);
}

// tell progress to stop
thread_message_queue_post($ch, 'done');

// we are done!
echo "All downloads done!\n";

