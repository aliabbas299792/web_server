#ifndef TCP_SERVER_CALLBACKS
#define TCP_SERVER_CALLBACKS

#include "web_server.h"

extern WOLFSSL_CTX *ctx;

int callback_send(WOLFSSL* ssl, char* buff, int sz, void* ctx);
int callback_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx);

void a_cb(int client_fd, server *tcp_server, void *custom_obj);
void r_cb(int client_fd, char *buffer, unsigned int length, server *tcp_server, void *custom_obj);
void w_cb(int client_fd, server *tcp_server, void *custom_obj);

#endif