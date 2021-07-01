# Web Server
## What it is
It's a multithreaded asynchronous web server made using liburing, to run on Linux operating systems with Kernel version >= 5.8.<br>
It looks for files to server in the `public` subdirectory.

## Configuration
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

## Libraries/header files used
Readerwriterqueue for a thread-safe concurrent queue:<br>
https://github.com/cameron314/readerwriterqueue

Liburing for a higher level wrapper over io_uring for asynchornous I/O:<br>
https://github.com/axboe/liburing

WolfSSL for SSL:<br>
https://github.com/wolfSSL/wolfssl

## Source code overview
- `./compile.sh` uses `cmake` and `make` to configure and build the project, and all the source code (and the `CMakeLists.txt` file) are in the `src` subdirectory.
- `*.tcc` files are used to provide template definitions, which are then included in header files
- `./src/header` contains the header files for use in all the other `*.cpp` and `*.tcc` files
- `./src/helper` has code used across files/more general in nature (such as the `SIGINT` handler)
- `./src/tcp_server` contains code for the main TCP/TLS server
- `./src/web_server` has the code for implementing HTTP and WebSockets
- `main.cpp` ties those together to provide a demo

### TCP Server
The TCP server, accessed via `tcp_tls_server::server<T>(...)` (where `T` is the server type, either `server_type::TLS` or `server_type::NON_TLS`) is an asyncrhonous simple TCP server, which takes as arguments some callbacks, the port to host on, a custom object (i.e the web server here), and a fullchain certificate and private key for TLS.<br>
The callbacks are:
- the accept callback (called when a new socket is accepted)
- the close callback (called when a socket is closed for some reason - used to basically clean up resources used by that client)
- the read callback (called with any data that was read from that socket)
- the write callback (called after something has been written to that socket, e.g a file)
- the event callback (called when something uses the `notify_event()` function on the server, used for any custom event logic)
- the custom read callback (called after something has been read from a file descriptor of your choosing using `custom_read_req(...)`
The web server plugs in the web server and using the callbacks interacts with any sockets.

### Web Server
Can support websockets and fulfills basic HTTP 1.0 requests, it takes no arguments, but requires you to call `set_tcp_server(...)` with a pointer to an instance of a TCP Server before it can be used. 

It makes use of a simple LRU cache, and, for use with the Central Web Server, has some lock free thread safe queues and functions to go along with them to safely send data back and forth between threads.

### Central Web Server
Currently broadcasts a small message periodically.

A small text message is turned into a websocket message and stored into a vector in the `data_store` class, and a pointer to that data and some information is broadcast to a set of threads, each with a separate Web Server and TCP server on them (the TCP server's use the same asynchronous backend since that bit is thread safe), and each Web Server broadcasts it to each connected websocket client.

After the message has been written to all the clients, each thread notifies the Central Web Server that the data has been written on that thread, and once all the Web Servers have notified the Central Web Server, that message is freed from the `data_store`'s vector.

This all goes to allow dealing with websocket connection interactions centrally if need be.

### Known issues
It's possible you get really high CPU usage due to `inotify_init()` failing and returning `-1`, so the `read`'s which followed would return `-9` and hence get stuck in a loop of failed reads, this may be due to errno `24`. Simply increase the inotify max user instances with `sudo sysctl fs.inotify.max_user_instances=256`, or some other command relating to increasing the number of possible `fd`'s on a system, this would probably fix the issue on the next startup.
