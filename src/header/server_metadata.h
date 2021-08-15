#ifndef SERVER_ENUMS
#define SERVER_ENUMS

#include <linux/limits.h>
#include <vector> //for vectors

enum class server_type { TLS, NON_TLS };

constexpr size_t inotify_read_size = 8192;  // to read many events, for large moves for example

// for use elsewhere too
constexpr int QUEUE_DEPTH = 256; //the maximum number of events which can be submitted to the io_uring submission queue ring at once, you can have many more pending requests though

namespace tcp_tls_server {
  enum class event_type{ ACCEPT, ACCEPT_READ, ACCEPT_WRITE, READ, READ_FINAL, WRITE, NOTIFICATION, CUSTOM_READ, KILL };

  constexpr int BACKLOG = 10; //max number of connections pending acceptance
  constexpr int READ_SIZE = 8192; //how much one read request should read
  constexpr int READ_BLOCK_SIZE = 8192; //how much to read from a file at once

  template<server_type T>
  class server_base; //forward declaration

  template<server_type T>
  class server;
}

//I don't want to type out the parameters twice, so I don't
#define      ACCEPT_CB_PARAMS int client_idx, tcp_tls_server::server<T> *tcp_server, void *custom_obj
#define       CLOSE_CB_PARAMS int client_idx, int broadcast_additional_info, tcp_tls_server::server<T> *tcp_server, void *custom_obj
#define        READ_CB_PARAMS int client_idx, char* buffer, unsigned int length, tcp_tls_server::server<T> *tcp_server, void *custom_obj
#define       WRITE_CB_PARAMS int client_idx, int broadcast_additional_info, tcp_tls_server::server<T> *tcp_server, void *custom_obj
#define       EVENT_CB_PARAMS tcp_tls_server::server<T> *tcp_server, void *custom_obj
#define CUSTOM_READ_CB_PARAMS int client_idx, int fd, std::vector<char> &&buff, size_t read_bytes, tcp_tls_server::server<T> *tcp_server, void *custom_obj

#endif
