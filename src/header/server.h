#ifndef SERVER
#define SERVER

#include <cstring> //for memset and strtok

#include <stdio.h> //perror and printf
#include <unistd.h> //also needed for syscall stuff
#include <netdb.h> //for networking stuff like addrinfo

#include <sys/stat.h> //fstat
#include <sys/ioctl.h> //ioctl
#include <sys/syscall.h> //syscall stuff parameters (as in like __NR_io_uring_enter/__NR_io_uring_setup)
#include <sys/mman.h> //for mmap

#include <liburing.h> //for liburing

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

#include <queue>
#include <vector> //for vectors
#include <iostream> //for string and iostream stuff
#include <unordered_map>
#include <unordered_set>

constexpr int BACKLOG = 10; //max number of connections pending acceptance
constexpr int READ_SIZE = 8192; //how much one read request should read
constexpr int QUEUE_DEPTH = 256; //the maximum number of events which can be submitted to the io_uring submission queue ring at once, you can have many more pending requests though
constexpr int READ_BLOCK_SIZE = 8192; //how much to read from a file at once

enum class event_type{ ACCEPT, ACCEPT_READ_SSL, ACCEPT_WRITE_SSL, READ, READ_SSL, WRITE, WRITE_SSL };

class server; //forward declaration

struct rw_cb_context {
  rw_cb_context(server *tcp_server = nullptr, int client_socket = -1) : tcp_server(tcp_server), client_socket(client_socket) {}
  server *tcp_server;
  int client_socket;
};

struct request {
  event_type event;
  int client_socket = 0;
  WOLFSSL *ssl = nullptr;
  int written = 0; //how much written so far
  int total_length = 0; //how much data is in the request, in bytes
  char *buffer = nullptr;
};

struct write_data {
  write_data(char *buff, int total_length) : buff(buff), total_length(total_length) {}
  char *buff;
  int total_length;
  int last_written = -1;
};

void fatal_error(std::string error_message);

class server; //forward declaration of server

typedef void(*accept_callback)(int client_socket, server *tcp_server, void *custom_obj);
typedef void(*read_callback)(int client_socket, char* buffer, unsigned int length, server *tcp_server, void *custom_obj);
typedef void(*write_callback)(int client_socket, server *tcp_server, void *custom_obj);

int tls_recv_helper(std::unordered_map<int, std::pair<char*, int>> *recvd_data, server *tcp_server, char *buff, int sz, int client_socket, bool accept);
int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx);
int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx);

class server{
  private:
    io_uring ring;
    int listener_fd;
    void *custom_obj; //it can be anything

    accept_callback a_cb = nullptr;
    read_callback r_cb = nullptr;
    write_callback w_cb = nullptr;

    int add_accept_req(int listener_fd, sockaddr_storage *client_address, socklen_t *client_address_length); //adds an accept request to the io_uring ring
    int add_write_req_continued(request *req, int offset); //only used for when writev didn't write everything
    int setup_listener(int port); //sets up the listener socket
    void serverLoop();

    bool running_server = false;
    
    //used internally for sending messages
    int add_read_req(int client_socket, bool accept = false); //adds a read request to the io_uring ring
    int add_write_req(int client_socket, char *buffer, unsigned int length, bool accept = false); //adds a write request using the provided request structure

    //TLS only variables and functions below
    //make the below 2 functions friends, so that they can access private data
    friend int tls_recv_helper(std::unordered_map<int, std::pair<char*, int>> *recvd_data, server *tcp_server, char *buff, int sz, int client_socket, bool accept);
    friend int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx);
    friend int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx);

    void tls_accept(int client_socket);
    
    bool is_tls = false;

    WOLFSSL_CTX *wolfssl_ctx = nullptr; //the wolfssl context to use here
    std::unordered_map<int, std::pair<char*, int>> accept_recv_data; //will store temporary data needed to negotiate a TLS connection
    std::unordered_map<int, int> accept_send_data; //will store temporary data needed to negotiate a TLS connection
    std::unordered_map<int, std::queue<write_data>> send_data; //will store data that is queued to be written by wolfSSL_write
    std::unordered_map<int, rw_cb_context> socket_to_context; //maps socket fd to the SSL context that I want set
    std::unordered_map<int, WOLFSSL*> socket_to_ssl; //maps a socket fd to an SSL object
    std::unordered_set<int> active_connections; //the fd's of active connections
  public:
    //accept callbacks for ACCEPT, READ and WRITE
    server(int listen_port, accept_callback a_cb = nullptr, read_callback r_cb = nullptr, write_callback w_cb = nullptr, void *custom_obj = nullptr);
    void setup_tls(std::string fullchain_location, std::string pkey_location); //sets up TLS using the certificate and private key provided
    void start(); //function to start the server

    void write_socket(int client_socket, char *buffer, unsigned int length);
    void close_socket(int client_socket);
};

#endif