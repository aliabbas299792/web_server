#ifndef TCP_SERVER_CALLBACKS
#define TCP_SERVER_CALLBACKS

#include "web_server.h"

using uchar = unsigned char;

template<server_type T>
void accept_cb(ACCEPT_CB_PARAMS);

template<server_type T>
void read_cb(READ_CB_PARAMS);

template<server_type T>
void write_cb(WRITE_CB_PARAMS);

#include "../web_server/callbacks.tcc" //template implementation file

#endif