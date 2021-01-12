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

TODO:
 - Watch out for any bugs