Simple web server made using liburing.
Looks for files in the public subdirectory.

A .config file is used to set the locations of the private key, the fullchain file, whether or not to use TLS, and what port to serve on.
For example:
```
TLS: yes
FULLCHAIN: /home/me/ssl/fullchain.cer
PKEY: /home/me/ssl/example.abc.key
PORT: 443
```

Explain bits:
 - In web_server.h, close_pending_ops_map is incremented when a write is made just before trying to close a websocket connection, and then in the write callback afterwards, they are decremented, up until the next decrement makes it 0 (i.e the close connection message has been sent)
 - websocket_frames in the same file is basically a map of client_socket to a vector of decoded data, which was extracted from 1 or more websocket frames, this is erased once the entire frame has been read
 - receiving_data is used when interfacing with the tls/tcp server, basically the server may not give enough data for an entire frame, so this map is used to store incomplete frames, until they are completed
 - In server.cpp, under the READ_SSL case, read_reqs are added on open sockets only if the amount read was 0 - this is because wolfSSL is prohibited from adding read_reqs like it does when establishing the connection, instead it just returns that it wants more data
   - - this means that we can use that while loop with wolfSSL_read, and we'll never get duplicate read_reqs on the same socket, as that can lead to weird behaviour like some read_reqs erasing data or placing data in the wrong order, but we delegate the initial read_req to the r_cb callback

TODO:
 - For websocket, use the autobahn test suite at some point when you want to make it more conforming, very handy to test the web server