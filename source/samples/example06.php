<?php
class TestThread {
	public function run () {
		self::static_print("hello from the thread");
		$this->print_ln("hello from the thread");
	}
	public function print_ln ($text) {
		echo $text."\n";
	}
	public static function static_print ($text) {
		$x = new TestThread();
		$x->print_ln($text);
	}
}
$test = new TestThread();
thread_create('TestThread::static_print', "Ohai");
echo "Done\n";
?>