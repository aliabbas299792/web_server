#ifndef WEB_SERVER
#define WEB_SERVER

#include "server.h"

using uchar = unsigned char;

struct receiving_data_info{
  receiving_data_info(int length = -1, std::vector<uchar> buffer = {}) : length(length), buffer(buffer) {}
  int length = -1;
  std::vector<uchar> buffer{};
};

class web_server{
  io_uring ring;

  std::string get_content_type(std::string filepath);

  int read_file(std::string filepath, std::vector<char>& buffer, int reserved_bytes);
public:
  web_server();
  int websocket_process();
  std::vector<char> read_file_web(std::string filepath, int responseCode = 200, bool accept_bytes = false);
  std::unordered_map<int, receiving_data_info> receiving_data; //for client socket to data
  std::unordered_map<int, std::vector<uchar>> data_store; //for client socket to data
  
  std::unordered_set<int> websocket_connections;
};

#endif