#ifndef SERVER
#define SERVER

#include <cstring> //for memset and strtok
#include <cstdlib> //for exit

#include <stdio.h> //perror and printf
#include <unistd.h> //also needed for syscall stuff
#include <netdb.h> //for networking stuff like addrinfo

#include <sys/stat.h> //fstat
#include <sys/ioctl.h> //ioctl
#include <sys/syscall.h> //syscall stuff parameters (as in like __NR_io_uring_enter/__NR_io_uring_setup)
#include <sys/mman.h> //for mmap

#include <liburing.h> //for liburing

#include <vector> //for vectors

#include <iostream> //for string and iostream stuff

#define BACKLOG 10 //max number of connections pending acceptance
#define READ_SIZE 8192 //how much one read request should read
#define PORT 3005
#define QUEUE_DEPTH 256 //the maximum number of events which can be submitted to the io_uring submission queue ring at once, you can have many more pending requests though
#define READ_BLOCK_SIZE 8192 //how much to read from a file at once

enum class event_type{ ACCEPT, READ, WRITE };

struct request {
  event_type event;
  int client_socket;
  int written = 0; //how much written so far
  int total_length = 0; //how much data is in the request, in bytes
  char *buffer = nullptr;
};

void fatal_error(std::string error_message);

class server{
  private:
    io_uring ring;
    int listener_fd;

    void (*accept_callback)(int client_fd, server *web_server) = nullptr;
    void (*read_callback)(int client_fd, char* buffer, unsigned int length, server *web_server) = nullptr;
    void (*write_callback)(int client_fd, server *web_server) = nullptr;

    int add_accept_req(int listener_fd, sockaddr_storage *client_address, socklen_t *client_address_length); //adds an accept request to the io_uring ring
    int add_write_req_continued(request *req, int offset); //only used for when writev didn't write everything
    int setup_listener(int port); //sets up the listener socket
    void serverLoop();
  public:
    //accept callbacks for ACCEPT, READ and WRITE
    server(void (*accept_callback)(int client_fd, server *web_server) = nullptr, void (*read_callback)(int client_fd, char *buffer, unsigned int length, server *web_server) = nullptr, void (*write_callback)(int client_fd, server *web_server) = nullptr);

    int add_read_req(int client_fd); //adds a read request to the io_uring ring
    int add_write_req(int client_fd, char *buffer, unsigned int length); //adds a write request using the provided request structure
    void read_wait(int client_fd); //for reading stuff
};

#endif