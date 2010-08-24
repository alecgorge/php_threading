PHP User-land threading support
===============================

Note about Windows
------------------
Don't forget to put deps/lib/pthreadVC2.dll in the same dir as your php.exe

Contributers
------------
[Moriyoshi Koizumi](http://github.com/moriyoshi): Inital code for GNU Pth and Linux

[Alec Gorge](http://github.com/alecgorge): Convert code to Pthreads so it can be used with Pthreads and Pthreads-win32 plus bug fixes that may or may not have been there in Moriyoshi's release.


Summary
-------
This adds a basic user-land threading support to the PHP scripting language. (both Windows and Linux).

A typical example (see samples directory for other ones that [should] work with the latest
trunk build on both Linux and Windows)::

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
		thread_create('sub', $i, $ch);
	}

	$i = 0;
	for (;;) {
		// send $i to $ch
		thread_message_queue_post($ch, $i++);
		sleep(1);
	}
	?>

Samples
-------
Samples can be found at (note that 2 & 3 are broken right now, fixes wanted):  [http://github.com/alecgorge/php_threading/tree/master/source/samples/](http://github.com/alecgorge/php_threading/tree/master/source/samples/)

Function list (these are your api docs presently)
-------------------------------------------------
	PHP_FUNCTION(thread_create);
	PHP_FUNCTION(thread_suspend);
	PHP_FUNCTION(thread_resume);
	PHP_FUNCTION(thread_join);
	PHP_FUNCTION(thread_mutex_create);
	PHP_FUNCTION(thread_mutex_acquire);
	PHP_FUNCTION(thread_mutex_release);
	PHP_FUNCTION(thread_message_queue_create);
	PHP_FUNCTION(thread_message_queue_post);
	PHP_FUNCTION(thread_message_queue_poll);
	PHP_FUNCTION(thread_message_queue_stop);
	PHP_FUNCTION(thread_message_slot_create);
	PHP_FUNCTION(thread_message_slot_post);
	PHP_FUNCTION(thread_message_slot_subscribe);

Why userland?
-------------
Using kernel threads in a poorly designed scripting runtime is kind of PITA
because it would require a plenty of synchronization primitives (such as
atomic memory operations) to be applied to the various portions of the code
that prevent unserialized accesses from multiple processors. Furthermore,
there are quite a few number of third-party libraries supported by PHP, most of
which are unlikely designed to be reentrant in general sense.

On the other hand, user threads are much easier to handle; it guarantees
every task runs in the same processor and every single operation is performed
coherently, thus there would be no need to patch the PHP runtime nor add a lot
of workarounds to the libraries as the behavior is much more predictable within
the same process context.

Known bugs
----------
* Not a leak, yet the memory consumption gradually increases everytime a thread
  gets created and won't decrease on its termination. This is due to poor
  quality of the code that manages the subthreads.

Current limitations
-------------------
* This is a experimental extension (TM) and there should still be many nasty
  bugs.
* A big turn-off -- threads cannot share a global context. Indeed they almost
  look like processes. You may call me a liar :)
* However, classes and functions are imported to subthreads.
* You can still pass data to a newly created thread by giving them as
  arguments to the entry point function.
* Data are always duplicated when they get passed between threads.
* Passing a container (an array or object) that contains one ore more
  references results in an error.
* Only a limited kinds of resources can be passed to another thread:

  * File handles
  * Sockets
  * Thread handles
  * Mutexes

* File handles and sockets are dup'licated on passage.

How to build PHP with thrading support (on Windows)
---------------------------------------------------
See included vcproj, everything is includd with a source code download. Build with VC2008. 2010 might work. Don't forget to put deps/lib/pthreadVC2.dll in the same dir as your php.exe

How to build PHP with threading support (on linux)
---------------------------------------
I have no clue, I am not a pthread gcc guru. These were the instructions for GNU Pth. If you are wise maybe you can adapt? 1 thing I do know is that you don't need to apply the patch because pthreads doesn't need it.

1. Put the archive into ``ext/threading``
2. Apply function_name_redefinition_fix.diff.patch to the PHP source. (-p0)
3. Run ``./buildconf`` at the top of the source tree
4. ``configure`` PHP with the following variables and flags::

     LDFLAGS="`pth-config --libs --ldflags`" \
     CPPFLAGS="`pth-config --cflags` -DPTH_SYSCALL_SOFT=1" \
     ./configure --with-tsrm-pth=pth-config \
                 --enable-maintainer-zts \
                 $@

5. make && make install
6. Enjoy!
