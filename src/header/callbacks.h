#ifndef TCP_SERVER_CALLBACKS
#define TCP_SERVER_CALLBACKS

#include "web_server.h"

using tls_server = server<server_type::TLS>;
using plain_server = server<server_type::NON_TLS>;
using tls_web_server = web_server<server_type::TLS>;
using plain_web_server = web_server<server_type::NON_TLS>;

using uchar = unsigned char;

template<server_type T>
void accept_cb(ACCEPT_CB_PARAMS);

template<server_type T>
void read_cb(READ_CB_PARAMS);

template<server_type T>
void write_cb(WRITE_CB_PARAMS);

template<server_type T>
void event_cb(EVENT_CB_PARAMS);

template<server_type T>
void custom_read_cb(CUSTOM_READ_CB_PARAMS);

#include "../web_server/callbacks.tcc" //template implementation file

#endif