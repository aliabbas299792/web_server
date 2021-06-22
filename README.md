# Web Server
### What it is
It's a web server made using liburing, to run on Linux operating systems with Kernel version >= 5.8.<br>
It looks for files to server in the `public` subdirectory.

### Configuration
A .config file is used to set the locations of the private key, the fullchain file, whether or not to use TLS, and what port to serve on for normal HTTP or TLS.
For example:
```
TLS: yes
FULLCHAIN: /home/me/ssl/fullchain.cer
PKEY: /home/me/ssl/example.abc.key
PORT: 80
TLS_PORT: 443
SERVER_THREADS: 10
```
It's supposed to be in the same directory as the server.

### Libraries/header files used
Readerwriterqueue for a thread-safe concurrent queue:<br>
https://github.com/cameron314/readerwriterqueue

Liburing for a higher level wrapper over io_uring for asynchornous I/O:<br>
https://github.com/axboe/liburing

WolfSSL for SSL:<br>
https://github.com/wolfSSL/wolfssl

### Source code overview
- `./compile.sh` uses `cmake` and `make` to configure and build the project, and all the source code (and the `CMakeLists.txt` file) are in the `src` subdirectory.
- `*.tcc` files are used to provide template definitions, which are then included in header files
- `./src/header` contains the header files for use in all the other `*.cpp` and `*.tcc` files
- `./src/common` has code used across files/more general in nature (such as the `SIGINT` handler)
- `./src/tcp_server` contains code for the main TCP/TLS server
- `./src/web_server` has the code for implementing HTTP and WebSockets
- `main.cpp` ties those together to provide a demo
