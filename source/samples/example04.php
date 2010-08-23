<?php
function sub($i, $ch) {
    for (;;) {
        // receive the message from $ch
        $a = thread_message_queue_poll($ch);
        printf("%d: %s\n", $i, $a);
    }
}

$ch = thread_message_queue_create();
for ($i = 0; $i < 10; $i++) {
    $rs[] = thread_create('sub', $i, $ch);
}

$i = 0;
for ($i = 0; $i < 20;$i++) {
    // send $i to $ch
    thread_message_queue_post($ch, $i++);
    sleep(1);
}
echo "Done. Now to join threads.\n";
die();


foreach($rs as $r) {
	echo "Joining: $r\n";
    thread_join($r);
}

echo "Done joining threads!";