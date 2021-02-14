#ifndef SERVER
#define SERVER

#include <cstring> //for memset and strtok

#include <stdio.h> //perror and printf
#include <netdb.h> //for networking stuff like addrinfo

#include <sys/syscall.h> //syscall stuff parameters (as in like __NR_io_uring_enter/__NR_io_uring_setup)
#include <sys/mman.h> //for mmap
#include <sys/eventfd.h>

#include <liburing.h> //for liburing

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

#include <queue>
#include <vector> //for vectors
#include <iostream> //for string and iostream stuff
#include <unordered_map>
#include <unordered_set>
#include <set> //ordered set for freed indexes, I believe it is sorted in ascending order which is exactly what we want
#include <chrono>
#include <mutex>

//I don't want to type out the parameters twice, so I don't
#define ACCEPT_CB_PARAMS int client_idx, server<T> *tcp_server, void *custom_obj
#define READ_CB_PARAMS int client_idx, char* buffer, unsigned int length, ulong custom_info, server<T> *tcp_server, void *custom_obj
#define WRITE_CB_PARAMS int client_idx, ulong custom_info, server<T> *tcp_server, void *custom_obj

constexpr int BACKLOG = 10; //max number of connections pending acceptance
constexpr int READ_SIZE = 8192; //how much one read request should read
constexpr int QUEUE_DEPTH = 256; //the maximum number of events which can be submitted to the io_uring submission queue ring at once, you can have many more pending requests though
constexpr int READ_BLOCK_SIZE = 8192; //how much to read from a file at once

enum class event_type{ ACCEPT, ACCEPT_READ, ACCEPT_WRITE, READ, WRITE, EVENTFD };
enum class server_type { TLS, NON_TLS };

template<server_type T>
class server_base; //forward declaration

template<server_type T>
class server;

//the wolfSSL callbacks
int tls_recv_helper(std::unordered_map<int, std::vector<char>> *recv_data, server<server_type::TLS> *tcp_server, char *buff, int sz, int client_socket, bool accept);
int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx);
int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx);

template<server_type T>
using accept_callback = void (*)(ACCEPT_CB_PARAMS);

template<server_type T>
using read_callback = void(*)(READ_CB_PARAMS);

template<server_type T>
using write_callback = void(*)(WRITE_CB_PARAMS);

struct write_data {
  write_data(std::vector<char> &&buff) : buff(buff) {}
  std::vector<char> buff{};
  int last_written = -1;
  int total_written = 0;
};

struct read_data {
  read_data(char *buffer = nullptr, size_t length = 0) : buffer(buffer), length(length) {}
  size_t length;
  char *buffer;
};

struct request {
  event_type event;
  int client_idx{};
  int ID{};
  write_data *w_data = nullptr;
  read_data r_data{};
};

struct client_base {
  int id{};
  int sockfd{};
  std::queue<write_data> send_data{};
  ulong custom_info = -1; //default custom_info is -1
};

template<server_type T>
struct client: client_base {};

template<>
struct client<server_type::NON_TLS>: client_base {};

template<>
struct client<server_type::TLS>: client_base {
    WOLFSSL *ssl = nullptr;
    int accept_last_written = -1;
    std::vector<char> recv_data{};
};

template<server_type T>
class server_base {
  protected:
    int listener_fd = 0;
    accept_callback<T> accept_cb = nullptr;
    read_callback<T> read_cb = nullptr;
    write_callback<T> write_cb = nullptr;

    static std::mutex init_mutex;
    static int shared_ring_fd; //pointer to a single io_uring ring fd, who's async backend is shared
    static int current_max_id; //max id of thread

    int thread_id = -1;
    io_uring ring;
    void *custom_obj; //it can be anything

    std::unordered_set<int> active_connections{};
    std::set<int> freed_indexes{}; //using a set to store free indexes instead
    std::vector<client<T>> clients{};

    int add_accept_req(int listener_fd, sockaddr_storage *client_address, socklen_t *client_address_length); //adds an accept request to the io_uring ring
    int setup_listener(int port); //sets up the listener socket

    bool running_server = false;

    //need it protected rather than private, since need to access from children
    int add_write_req(int client_idx, event_type event, write_data *w_data); //adds a write request using the provided request structure
    //used internally for sending messages
    int add_read_req(int client_idx, event_type event); //adds a read request to the io_uring ring

    void register_eventfd(int eventfd); //registers an eventfd
    
    int setup_client(int client_idx);

    int event_fd = eventfd(0, 0); //used to awaken this thread for some event
    void event_read(); //will set a read request for the eventfd
  public:
    server_base();
    void start(); //function to start the server

    void read_connection(int client_idx, ulong custom_info = 0);
    void notify_event();
};

template<>
class server<server_type::NON_TLS>: public server_base<server_type::NON_TLS> {
  private:
    friend class server_base;
    void server_loop();

    int add_write_req_continued(request *req, int offset); //only used for when writev didn't write everything
  public:
    server(int listen_port, 
      accept_callback<server_type::NON_TLS> a_cb = nullptr,
      read_callback<server_type::NON_TLS> r_cb = nullptr,
      write_callback<server_type::NON_TLS> w_cb = nullptr,
      void *custom_obj = nullptr
    );

    void write_connection(int client_idx, std::vector<char> &&buff, ulong custom_info = 0); //writing depends on TLS or SSL, unlike read
    void close_connection(int client_idx); //closing depends on what resources need to be freed
};

template<>
class server<server_type::TLS>: public server_base<server_type::TLS> {
  private:
    friend int tls_recv_helper(server<server_type::TLS> *tcp_server, int client_idx, char *buff, int sz, bool accept);
    friend int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx);
    friend int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx);

    friend class server_base;
    void tls_accept(int client_socket);
    void server_loop();

    WOLFSSL_CTX *wolfssl_ctx = nullptr;
  public:
    server(
      int listen_port,
      std::string fullchain_location,
      std::string pkey_location,
      accept_callback<server_type::TLS> a_cb = nullptr,
      read_callback<server_type::TLS> r_cb = nullptr,
      write_callback<server_type::TLS> w_cb = nullptr,
      void *custom_obj = nullptr
    );

    void write_connection(int client_idx, std::vector<char> &&buff, ulong custom_info = 0); //writing depends on TLS or SSL, unlike read
    void close_connection(int client_idx); //closing depends on what resources need to be freed
};

#include "../tcp_server/server_base.tcc" //template implementation file

#endif