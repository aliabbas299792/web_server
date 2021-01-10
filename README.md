Simple web server made using liburing.
Looks for files in the public subdirectory.

TODO:
 - Fix a memory leak everywhere, do this by switching to shared pointers, and switch away slowly if you must
 - There's also something corrupting a map, program received signal SIGFPE (SO discussion (https://stackoverflow.com/questions/14097924/arithmetic-exception-in-gdb-but-im-not-dividing-by-zero)[here])