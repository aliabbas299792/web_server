using WOLFSSL = int;
using io_uring = int;

#include <cstring> //for memset and strtok

#include <stdio.h> //perror and printf
#include <netdb.h> //for networking stuff like addrinfo

#include <sys/syscall.h> //syscall stuff parameters (as in like __NR_io_uring_enter/__NR_io_uring_setup)
#include <sys/mman.h> //for mmap

#include <queue>
#include <vector> //for vectors
#include <iostream> //for string and iostream stuff
#include <unordered_map>
#include <unordered_set>
#include <chrono>

constexpr int BACKLOG = 10; //max number of connections pending acceptance
constexpr int READ_SIZE = 8192; //how much one read request should read
constexpr int QUEUE_DEPTH = 256; //the maximum number of events which can be submitted to the io_uring submission queue ring at once, you can have many more pending requests though
constexpr int READ_BLOCK_SIZE = 8192; //how much to read from a file at once

enum class event_type{ ACCEPT, ACCEPT_READ_SSL, ACCEPT_WRITE_SSL, READ, READ_SSL, WRITE, WRITE_SSL };
enum class server_type { TLS, NON_TLS };

template<server_type T, typename U>
class server_base; //forward declaration

template<server_type T>
class server: server_base<T, server<T>>{};

template<server_type T>
using accept_callback = void (*)(int client_socket, server<T> *tcp_server, void *custom_obj);

template<server_type T>
using read_callback = void(*)(int client_socket, char* buffer, unsigned int length, server<T> *tcp_server, void *custom_obj);

template<server_type T>
using write_callback = void(*)(int client_socket, server<T> *tcp_server, void *custom_obj);

struct request {
  event_type event;
  int client_socket = 0;
  WOLFSSL *ssl = nullptr;
  int written = 0; //how much written so far
  int total_length = 0; //how much data is in the request, in bytes
  char *buffer = nullptr;
};

struct write_data {
  write_data(std::vector<char> &&buff) : buff(buff) {}
  std::vector<char> buff;
  int last_written = -1;
};

struct client_base {
    int id = 0;
    int sockfd = 0;
    std::queue<write_data> send_data{};
};

template<server_type T>
struct client: client_base {};

template<>
struct client<server_type::TLS>: client_base {};

template<>
struct client<server_type::NON_TLS>: client_base {
    WOLFSSL *ssl = nullptr;
    int accept_last_written = 0;
    std::vector<char> accept_recv_data{};
};

template<server_type T, typename U>
class server_base {
  protected:
    int listener_fd = 0;
    accept_callback<T> a_cb;
    read_callback<T> r_cb;
    write_callback<T> w_cb;
  private:
    io_uring ring;
    void *custom_obj; //it can be anything

    int add_accept_req(int listener_fd, sockaddr_storage *client_address, socklen_t *client_address_length); //adds an accept request to the io_uring ring
    int add_write_req_continued(request *req, int offset); //only used for when writev didn't write everything
    int setup_listener(int port); //sets up the listener socket

    bool running_server = false;
    
    //used internally for sending messages
    int add_read_req(int client_socket, bool accept = false); //adds a read request to the io_uring ring
    int add_write_req(int client_socket, char *buffer, unsigned int length, bool accept = false); //adds a write request using the provided request structure

    std::unordered_set<int> active_connections{};
    std::queue<int> freed_indexes{};
    std::vector<client<T>> clients{};
  public:
    void start(); //function to start the server

    void write_socket(int client_socket, std::vector<char> &&buff);
    void read_socket(int client_socket);
    void close_socket(int client_socket);
};

template<>
class server<server_type::NON_TLS>: public server_base<server_type::NON_TLS, server<server_type::NON_TLS>> {
  private:
    friend class server_base;
    void serverLoop();
  public:
    server(int listen_port,
      accept_callback<server_type::NON_TLS> a_cb = nullptr,
      read_callback<server_type::NON_TLS> r_cb = nullptr,
      write_callback<server_type::NON_TLS> w_cb = nullptr,
      void *custom_obj = nullptr
    ){};
};

template<>
class server<server_type::TLS>: public server_base<server_type::TLS, server<server_type::TLS>> {
  private:
    friend int tls_recv_helper(std::unordered_map<int, std::vector<char>> *recv_data, server *tcp_server, char *buff, int sz, int client_socket, bool accept);
    friend int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx);
    friend int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx);

    friend class server_base;
    void tls_accept(int client_socket);
    void serverLoop();
  public:
    server(
      int listen_port,
      std::string fullchain_location,
      std::string pkey_location,
      accept_callback<server_type::TLS> a_cb = nullptr,
      read_callback<server_type::TLS> r_cb = nullptr,
      write_callback<server_type::TLS> w_cb = nullptr,
      void *custom_obj = nullptr
    ){};
};

template<server_type T, typename U>
void server_base<T, U>::start(){ //function to run the server
  std::cout << "Running server\n";
  if(!running_server) static_cast<server<T>*>(this)->serverLoop();
}

void server<server_type::TLS>::serverLoop(){
    std::cout << "hello world...\n";
}

int main(){
    auto thing = server<server_type::TLS>(10, "a", "b");
    thing.start();
}