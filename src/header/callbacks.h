#ifndef TCP_SERVER_CALLBACKS
#define TCP_SERVER_CALLBACKS

#include "web_server.h"

void a_cb(int client_fd, server *tcp_server, void *custom_obj);
void r_cb(int client_fd, char *buffer, unsigned int length, server *tcp_server, void *custom_obj);
void w_cb(int client_fd, server *tcp_server, void *custom_obj);

#endif