#ifndef SERVER_ENUMS
#define SERVER_ENUMS

#include <vector> //for vectors

enum class event_type{ ACCEPT, ACCEPT_READ, ACCEPT_WRITE, READ, WRITE, EVENTFD, CUSTOM_READ };
// server signals < 10 are reserved, signals >= 10 are just for notification
enum server_signals { KILL = 1 };
enum class server_type { TLS, NON_TLS };

//I don't want to type out the parameters twice, so I don't
#define      ACCEPT_CB_PARAMS int client_idx, server<T> *tcp_server, void *custom_obj
#define       CLOSE_CB_PARAMS int client_idx, int broadcast_additional_info, server<T> *tcp_server, void *custom_obj
#define        READ_CB_PARAMS int client_idx, char* buffer, unsigned int length, server<T> *tcp_server, void *custom_obj
#define       WRITE_CB_PARAMS int client_idx, int broadcast_additional_info, server<T> *tcp_server, void *custom_obj
#define       EVENT_CB_PARAMS server<T> *tcp_server, void *custom_obj
#define CUSTOM_READ_CB_PARAMS int client_idx, int fd, std::vector<char> &&buff, server<T> *tcp_server, void *custom_obj

constexpr int BACKLOG = 10; //max number of connections pending acceptance
constexpr int READ_SIZE = 8192; //how much one read request should read
constexpr int QUEUE_DEPTH = 256; //the maximum number of events which can be submitted to the io_uring submission queue ring at once, you can have many more pending requests though
constexpr int READ_BLOCK_SIZE = 8192; //how much to read from a file at once

template<server_type T>
class server_base; //forward declaration

template<server_type T>
class server;

#endif