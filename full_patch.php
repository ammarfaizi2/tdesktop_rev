<?php
// SPDX-License-Identifier: GPL-2.0
/**
 * Stupid patch Telegram binary (just for fun)
 * @author Ammar Faizi <ammarfaizi2@gmail.com> https://www.facebook.com/ammarfaizi2
 */

if (PHP_SAPI != "cli") {
	printf("This script can only be run in CLI environment "
		."(current environment: %s)\n", PHP_SAPI);
	exit(1);
}


if (count($_SERVER["argv"]) != 2) {
	printf("Usage: php %s /path/to/Telegram\n", $_SERVER["argv"][0]);
	exit(1);
}


$origBinFile = $_SERVER["argv"][1];
$targetFile  = __DIR__."/tgnew";

if (!file_exists($origBinFile)) {
	printf("No such file or directory: %s\n", $origBinFile);
	exit(1);
}

if (!is_readable($origBinFile)) {
	printf("Permission denied: %s\n", $origBinFile);
	exit(1);
}


/*
 * Run it with:
 *  php -d memory_limit=-1 full_patch.php
 *
 * Don't edit the below code, unless you really know what you are doing!
 * Wrong patch may crash your Telegram!
 *
 * Tested on Telegram 2.8.1 Linux x86-64
 */

// Telegram binary
$bin  = file_get_contents($origBinFile);


// Patch hide typing
$find = base64_decode(
<<<BASE64
VUiJ5UFXQVZBVUFUQYnUU0iJ80iB7BgCAABIib0I/v//6Cpl/f+EwHQWSI1l2FtBXEFdQV5BX13D
Dx+AAAAAAA==
BASE64
);
$repl = base64_decode(
<<<BASE64
w0iJ5UFXQVZBVUFUQYnUU0iJ80iB7BgCAABIib0I/v//6Cpl/f+EwHQWSI1l2FtBXEFdQV5BX13D
Dx+AAAAAAA==
BASE64
);
$orig = $bin;
$bin  = str_replace($find, $repl, $bin, $n);
printf("(%s) Patch hide typing: (str_replace returned: %d)\n",
	($n == 1 ? "Success" : "Failed"), $n);
if ($n != 1)
	$bin = $orig;


// Patch anti delete message (group and channel)
$find = base64_decode(
<<<BASE64
TIn/6Ouw+v5Iie9IicPoADz+/0hjcwxIi3h46LNGov9IhcAPhLoOAACAvcgCAAAAD4WtDgAAgLhI
AgAAAHWoi0sci1MYSI246AEAAE2J+EiJxuh8Mab/
BASE64
);
$repl = str_repeat("\x90", strlen($find));
$orig = $bin;
$bin  = str_replace($find, $repl, $bin, $n);
printf("(%s) Patch anti delete message (group and channel) ".
	"(str_replace returned: %d)\n", ($n == 1 ? "Success" : "Failed"), $n);
if ($n != 1)
	$bin = $orig;


// Patch anti delete message (private)
$find = base64_decode("TIn/6C22+v5MiflIie+LUByLcBjobMH+/w==");
$repl = str_repeat("\x90", strlen($find));
$bin  = str_replace($find, $repl, $bin, $n);
printf("(%s) Patch anti delete message (private): (str_replace returned: %d)\n",
	($n == 1 ? "Success" : "Failed"), $n);
if ($n != 1)
	$bin = $orig;

printf("Syncing to %s...\n", $targetFile);
file_put_contents($targetFile, $bin);
