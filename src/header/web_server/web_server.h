#ifndef WEB_SERVER
#define WEB_SERVER

#include "../server.h"
#include "../utility.h"

#include "common_structs_enums.h"
#include "cache.h"

#include "../../vendor/readerwriterqueue/atomicops.h"
#include "../../vendor/readerwriterqueue/readerwriterqueue.h"

using uchar = unsigned char;

enum websocket_non_control_opcodes {
  text_frame = 0x01,
  binary_frame = 0x02,
  close_connection = 0x08,
  ping = 0x09,
  pong = 0xA
};

enum class message_type {
  websocket_broadcast
};

struct receiving_data_info{
  receiving_data_info(int length = -1, std::vector<char> buffer = {}) : length(length), buffer(buffer) {}
  int length = -1;
  std::vector<char> buffer{};
};

struct ws_client {
  int currently_writing = 0; //items it is currently writing
  bool close = false; //should this socket be closed
  std::vector<char> websocket_frames{};
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
  //
  ////generally useful functions and variables
  //
  io_uring ring;
  server<T> *tcp_server = nullptr;

  std::string get_content_type(std::string filepath);

  //
  ////http stuff
  //

  //
  ////websocket stuff////
  //
  
  //reading data from connections
  ulong get_ws_frame_length(const char *buffer); //helper function which reads the websocket header to get the length of the message
  std::pair<int, std::vector<char>> decode_websocket_frame(std::vector<char> &&data); //decodes a single full websocket frame
  std::pair<int, std::vector<std::vector<char>>> get_ws_frames(char *buffer, int length, int ws_client_idx); //gets any full websocket frames possible
  std::vector<char> make_ws_frame(const std::string &packet_msg, websocket_non_control_opcodes opcode);

  //writing data to connections
  void websocket_write(int ws_client_idx, std::vector<char> &&buff);
  
  //related to opening/closing connections
  std::string get_accept_header_value(std::string input); //gets the appropriate header value from the websocket connection request
  int new_ws_client(int client_idx); //makes a new websocket client
  bool close_ws_connection_req(int ws_client_idx, bool client_already_closed = false); //puts in a request to close this websocket connection
  bool close_ws_connection_potential_confirm(int ws_client_idx); //actually closes the websocket connection (it's sent a close notification)

  //where data about connections is stored
  std::set<int> freed_indexes{}; //set of free indexes for websocket client stuff
  std::vector<ws_client> websocket_clients{};
  
  //
  ////communication between threads////
  //

  //lock free queues used to transport data between threads
  moodycamel::ReaderWriterQueue<void*> to_server_queue{};
  moodycamel::ReaderWriterQueue<void*> to_program_queue{};
public:
  web_server() {};

  void set_tcp_server(server<T> *tcp_server); //required to be called to ensure pointer to TCP server is present

  void new_tcp_client(int client_idx);
  void kill_tcp_client(int client_idx);

  void close_connection(int client_idx);

  std::vector<tcp_client> tcp_clients{}; //storing additional data related to the client_idxs passed to this layer

  // void post_message(message_type type, )
  
  //
  ////http public methods
  //

  //responding to get requests
  bool get_process(std::string &path, bool accept_bytes, const std::string& sec_websocket_key, int client_idx);
  //sending files
  bool send_file_request(int client_idx, const std::string &filepath, bool accept_bytes, int response_code);
  //checking if it's a valid HTTP request
  bool is_valid_http_req(const char* buff, int length);
  //the cache
  cache<5> web_cache{}; //cache of 5 items
  
  //
  ////public websocket stuff
  //

  //websocket public methods
  void websocket_process_read_cb(int client_idx, char *buffer, int length);
  bool websocket_process_write_cb(int client_idx); //returns whether or not this was used
  void websocket_accept_read_cb(const std::string& sec_websocket_key, const std::string &path, int client_idx); //used in the read callback to accept web sockets

  //websocket data
  std::unordered_set<int> all_websocket_connections{}; //this is used for the duration of the connection (even after we've sent the close request)
  std::unordered_set<int> active_websocket_connections_client_idxs{}; //this is only active up until we call a close request, has client_idx
};

#include "../../web_server/web_server.tcc"
#include "../../web_server/websockets.tcc"

#endif