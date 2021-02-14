#ifndef WEB_SERVER
#define WEB_SERVER

#include "server.h"
#include "utility.h"

#include "../vendor/readerwriterqueue/atomicops.h"
#include "../vendor/readerwriterqueue/readerwriterqueue.h"

using uchar = unsigned char;

enum websocket_non_control_opcodes {
  text_frame = 0x01,
  binary_frame = 0x02,
  close_connection = 0x08,
  ping = 0x09,
  pong = 0xA
};

struct receiving_data_info{
  receiving_data_info(int length = -1, std::vector<uchar> buffer = {}) : length(length), buffer(buffer) {}
  int length = -1;
  std::vector<uchar> buffer{};
};

struct ws_client {
  int closing_state{};
  std::vector<uchar> websocket_frames{};
  receiving_data_info receiving_data{};
  int id = 0; //in case we use io_uring later
  int client_idx{}; //for the TCP/TLS layer
};

template<server_type T>
class central_web_server {
private:

public:
  
};

template<server_type T>
class web_server{
  io_uring ring;

  std::string get_content_type(std::string filepath);

  int read_file(std::string filepath, std::vector<char>& buffer, int reserved_bytes);

  server<T> *tcp_server = nullptr;

  std::string get_accept_header_value(std::string input);
  ulong get_ws_frame_length(const char *buffer);
  std::vector<char> make_ws_frame(const std::string &packet_msg, websocket_non_control_opcodes opcode);
  std::pair<int, std::vector<uchar>> decode_websocket_frame(std::vector<uchar> data);
  std::pair<int, std::vector<std::vector<uchar>>> get_ws_frames(char *buffer, int length, int ws_client_idx);
  
  int new_ws_client(int client_idx);
  bool close_ws_connection_req(int ws_client_idx, bool client_already_closed = false);
  bool close_ws_connection_confirm(int ws_client_idx);
  void websocket_write(int ws_client_idx, std::vector<char> &&buff);
  
  moodycamel::ReaderWriterQueue<void*> to_server_queue{};
  moodycamel::ReaderWriterQueue<void*> to_program_queue{};
public:
  web_server();

  //http public methods
  bool is_valid_http_req(const char* buff, int length);
  std::vector<char> read_file_web(std::string filepath, int responseCode = 200, bool accept_bytes = false);

  bool get_process(std::string &path, bool accept_bytes, const std::string& sec_websocket_key, int client_idx, server<T> *tcp_server);
  
  //websocket public methods
  void websocket_process_read_cb(int ws_client_idx, char *buffer, int length);
  bool websocket_process_write_cb(int ws_client_idx); //returns whether or not this was used
  void websocket_accept_read_cb(const std::string& sec_websocket_key, const std::string &path, int client_idx, server<T> *tcp_server); //used in the read callback to accept web sockets

  std::unordered_set<int> all_websocket_connections; //this is used for the duration of the connection (even after we've sent the close request)
  std::unordered_set<int> active_websocket_connections; //this is only active up until we call a close request

  std::set<int> freed_indexes{}; //set of free indexes for websocket client stuff
  std::vector<ws_client> websocket_clients{};
};

#include "../web_server/web_server.tcc"
#include "../web_server/websockets.tcc"

#endif