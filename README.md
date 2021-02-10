Simple web server made using liburing.
Looks for files in the public subdirectory.

A .config file is used to set the locations of the private key, the fullchain file, whether or not to use TLS, and what port to serve on for normal HTTP or TLS.
For example:
```
TLS: yes
FULLCHAIN: /home/me/ssl/fullchain.cer
PKEY: /home/me/ssl/example.abc.key
PORT: 80
TLS_PORT: 443
```

Structure:
```
  server.h     -> included in `server_tls.cpp` and `server_non_tls.cpp` to implement the functions for those specialised bits
               -> included by `server_base.tcc` for the `server_base` class
               -> includes `server_base.tcc` because need to included template implementation files

  callbacks.h  -> included by `callbacks.tcc` for the header files and whatnot
               -> includes `callbacks.tcc` for the template implementation stuff

  utility.h    -> included by most files for various utility functions

  web_server.h -> included by `callbacks.h` for the asbtracted web server stuff
```

TCC files aren't compiled directly, they are included in header files.

You can attach `custom_info` to a client using `read_connection` or `write_connection`, this modifies the member variable immediately, order of change doesn't matter, it always defaults to 0, so provide it if you need it.

TODO:
 - For websocket, use the autobahn test suite at some point when you want to make it more conforming, very handy to test the web server