#ifndef WEB_SERVER
#define WEB_SERVER

#include "server.h"

class web_server{
  io_uring ring;

  std::string get_content_type(std::string filepath);

  int read_file(std::string filepath, std::vector<char>& buffer, int reserved_bytes);
public:
  web_server();
  int websocket_process();
  std::vector<char> read_file_web(std::string filepath, int responseCode = 200, bool accept_bytes = false);
};

#endif