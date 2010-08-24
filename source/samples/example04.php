<?php
function sub($i, $ch) {
	for (;;) {
        // receive the message from $ch
        $a = thread_message_queue_poll($ch);

		// TRIPLE COMPARISION IS IMPORTANT!!!!
		if($a === 'PHP_THREAD_POLL_STOP') {
			break;
		}
		
        printf("thread %02d: %02s\n", $i, $a * 2);
    }
	sleep(2);
	printf("thread %02s is done.\n", $i);
}

$ch = thread_message_queue_create();
for ($i = 0; $i < 20; $i++) {
    $rs[] = thread_create('sub', $i, $ch);
}

for ($i = 0; $i < 20;$i++) {
    // send $i to $ch
    thread_message_queue_post($ch, $i);
    usleep(200000);
}
// threads are still waiting for a message. tell them that we are stopping
thread_message_queue_stop($ch);

// after threads recieve the stop message, they break out of the for loop and sleep
echo "Done sending messages. Threads are sleeping for 2 seconds.\n";

for ($i = 0; $i < 10;$i++) {
	// thread_join waits for the the thread to finish before continuing
	thread_join($rs[$i]);
}

echo "All threads are complete.";