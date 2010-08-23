<?php
// Set time limit to indefinite execution 
set_time_limit (0); 

// Set the ip and port we will listen on 
$address = '127.0.0.1'; 
$port = 9000; 

// Create a TCP Stream socket 
$sock = socket_create(AF_INET, SOCK_STREAM, 0); 
// Bind the socket to an address/port 
socket_bind($sock, $address, $port) or die('Could not bind to address'); 
// Start listening for connections 
socket_listen($sock); 

/* Accept incoming requests and handle them as child processes */ 
$client = socket_accept($sock); 

// Read the input from the client &#8211; 1024 bytes 
$input = socket_read($client, 1024); 

// Strip all white spaces from input 
$output = ereg_replace("[ \t\n\r]","",$input).chr(0); 

// Display output back to client 
socket_write($client, $output); 

// Close the client (child) socket 
socket_close($client); 

// Close the master sockets 
socket_close($sock); 

exit();

function echoing($fd) {
	echo "hi!";
	while (!feof($fd)) {
		fwrite($fd, fgets($fd));
    }
	fclose($fd);
	echo "done\n";
	sleep(5);
}

$ssock = stream_socket_server('tcp://127.0.0.1:8000');
assert($ssock !== false);
  
// cool infinite loop
for (;;) {
	$csock = stream_socket_accept($ssock);
	echo "before creating thread!\n";
	if($csock) {
		thread_create('echoing', $csock);
		echo "thread done.\n";
		
		// everything is copied so we can close this connection
		fclose($csock);
	}
	else {
		trigger_error('Invalid csock', E_USER_WARNING);
	}
}

?>