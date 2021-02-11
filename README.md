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
 - Run the TLS server for a while and test with lots of data, there might be a memory leak, but can't really find it
 - For websocket, use the autobahn test suite at some point when you want to make it more conforming, very handy to test the web server

 Simple JS function to get stats from ws_test_output.txt:
 ```js
const mean = arr => arr.reduce((sum, curr) => sum += curr, 0)/arr.length

const std_dev = arr => {
    const mean_val = mean(arr)
    const variance = arr.reduce((sum, curr) => sum += Math.pow(curr - mean_val, 2), 0)/(arr.length - 1)
    return Math.sqrt(variance)
}

function get_stats(input){
  const befores = input.match(/Before:[ ]+total kB[ ]+\d+/g).map(item => parseInt([...item.match(/[ ]+(\d+)/)][1]));
  const afters = input.match(/After:[ ]+total kB[ ]+\d+/g).map(item => parseInt([...item.match(/[ ]+(\d+)/)][1]));

  const befores_stats = [ mean(befores), std_dev(befores) ];
  const afters_stats = [ mean(afters), std_dev(afters) ];
  
  console.log("Befores:\n" + "\tMean: " + befores_stats[0] + "\n\tStandard dev: " + befores_stats[1]);
  console.log("Afters:\n" + "\tMean: " + afters_stats[0] + "\n\tStandard dev: " + afters_stats[1]);
}
```