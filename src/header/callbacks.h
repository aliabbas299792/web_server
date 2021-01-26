#ifndef TCP_SERVER_CALLBACKS
#define TCP_SERVER_CALLBACKS

#include "web_server.h"

using uchar = unsigned char;

template<server_type T>
void a_cb(int client_fd, server<T> *tcp_server, void *custom_obj);

template<server_type T>
void r_cb(int client_fd, char *buffer, unsigned int length, server<T> *tcp_server, void *custom_obj);

template<server_type T>
void w_cb(int client_fd, server<T> *tcp_server, void *custom_obj);

#endif