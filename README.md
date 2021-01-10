Simple web server made using liburing.
Looks for files in the public subdirectory.

TODO:
 - Fix a memory leak everywhere, do this by switching to shared pointers, and switch away slowly if you must
 - It appears all bugs are fixed now, but be on the lookout for map related errors
 - Emphasis on finding and fixing the memory leak though