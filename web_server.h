#ifndef WEB_SERVER
#define WEB_SERVER

#include "server.h"

#include <unistd.h> //read
#include <sys/stat.h> //fstat
#include <fcntl.h> //open
#include <sys/types.h> //O_RDONLY

typedef struct stat stat_struct;

void a_cb(int client_fd, server *tcp_server, void *custom_obj);
void r_cb(int client_fd, char *buffer, unsigned int length, server *tcp_server, void *custom_obj);
void w_cb(int client_fd, server *tcp_server, void *custom_obj);

class web_server{
  io_uring ring;

  int read_file(std::string filepath, char **buffer, int reserved_bytes = 0);
  long int get_file_size(int file_fd);
  std::string get_content_type(std::string filepath);
public:
  web_server();
  int read_file_web(std::string filepath, char **buffer, int responseCode = 200, bool accept_bytes = false);
};

#endif