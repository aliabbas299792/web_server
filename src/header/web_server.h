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
  int currently_writing = 0; //items it is currently writing
  bool close = false; //should this socket be closed
  std::vector<uchar> websocket_frames{};
  receiving_data_info receiving_data{};
  int id = 0; //in case we use io_uring later
  int client_idx{}; //for the TCP/TLS layer
};

struct tcp_client {
  std::string last_requested_read_filepath{}; //the last filepath it was asked to read
  int ws_client_idx{};
  std::string last_requested_filepath{}; //the last file requested in general
};

template<server_type T>
class central_web_server {
private:

public:
  
};

struct cache_item {
  std::vector<char> buffer{};
  uint64_t timestamp{};
  // int lock_number{}; //number of times this has been locked, if non zero then this item should NOT be removed (in use)
  int next_item_idx = -1;
  int prev_item_idx = -1;
};

struct cache_fetch_item {
  cache_fetch_item(bool found, char *buff_ptr, size_t size = -1) : found(found), buff(buff_ptr), size(size) {}
  bool found = false;
  size_t size{};
  char *buff{};
};

template<int N>
class cache{
private:
  std::unordered_map<std::string, int> filepath_to_cache_idx{};
  std::unordered_map<int, std::string> cache_idx_to_filepath{};
  std::array<cache_item, N> cache_buffer{};
  std::unordered_set<int> free_idxs{}; //only used at the beginning of the program
  std::unordered_map<int, int> client_idx_to_cache_idx{};
  int highest_idx = -1;
  int lowest_idx = -1;
public:
  cache(){ //only works for cache's which are greater than 1 in size
    for(int i = 0; i < cache_buffer.size(); i++)
      free_idxs.insert(i);
  }

  cache_fetch_item fetch_item(const std::string &filepath, int client_idx){
    if(filepath_to_cache_idx.count(filepath)) {
      auto current_idx = filepath_to_cache_idx[filepath];
      auto &item = cache_buffer[current_idx];

      client_idx_to_cache_idx[client_idx] = current_idx; //mapping to the idx that the client_idx is locking

      if(item.next_item_idx == -1) //cannot promote highest one more
        return { true, &(cache_buffer[current_idx].buffer[0]), cache_buffer[current_idx].buffer.size() };
      
      //otherwise promote current item to top

      if(item.prev_item_idx != -1){ //first though we've gotta remove it from its current position
        auto &prev_item = cache_buffer[item.prev_item_idx];
        auto &next_item = cache_buffer[item.next_item_idx];
        prev_item.next_item_idx = item.next_item_idx;
        next_item.prev_item_idx = item.prev_item_idx;
      }else{
        auto &next_item = cache_buffer[item.next_item_idx];
        next_item.prev_item_idx = -1;
        lowest_idx = item.next_item_idx;
      }

      cache_buffer[highest_idx].next_item_idx = current_idx;
      cache_buffer[current_idx].next_item_idx = -1;
      cache_buffer[current_idx].prev_item_idx = highest_idx;

      highest_idx = current_idx; //current item is promoted to the top

      return { true, &(cache_buffer[current_idx].buffer[0]), cache_buffer[current_idx].buffer.size() };
    }else{
      return { false, nullptr };
    }
  }
  
  bool try_insert_item(int client_idx, const std::string &filepath, std::vector<char> &&buff){
    int current_idx = -1;

    if(free_idxs.size()){ //if free idxs available
      current_idx = *free_idxs.cbegin();
      free_idxs.erase(current_idx);

      if(lowest_idx == -1)
        lowest_idx = current_idx;
    }else{
      auto &lowest_item = cache_buffer[lowest_idx];
      const auto new_lowest_idx = lowest_item.next_item_idx;

      cache_buffer[lowest_item.next_item_idx].prev_item_idx = -1; //2nd lowest is now lowest
      cache_buffer[lowest_idx] = cache_item(); //we are reusing the lowest item

      filepath_to_cache_idx.erase(cache_idx_to_filepath[lowest_idx]);
      cache_idx_to_filepath.erase(lowest_idx);
      
      current_idx = lowest_idx;
      lowest_idx = new_lowest_idx;
    }

    if(current_idx != -1){
      auto &current_item = cache_buffer[current_idx];
      
      if(highest_idx != -1)
        cache_buffer[highest_idx].next_item_idx = current_idx;
      
      filepath_to_cache_idx[filepath] = current_idx;
      cache_idx_to_filepath[current_idx] = filepath;

      current_item.prev_item_idx = highest_idx; //promote to highest
      current_item.next_item_idx = -1;
      highest_idx = current_idx; //new highest position
      current_item.buffer = std::move(buff); //populate the buffer
      return true;
    }else{
      return false;
    }
  }

  void finished_with_item(int client_idx){
    const auto cache_idx = client_idx_to_cache_idx[client_idx];
    // cache_buffer[cache_idx].lock_number--;
  }
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
  std::pair<int, std::vector<uchar>> decode_websocket_frame(std::vector<uchar> data); //decodes a single full websocket frame
  std::pair<int, std::vector<std::vector<uchar>>> get_ws_frames(char *buffer, int length, int ws_client_idx); //gets any full websocket frames possible
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
  web_server();

  void set_tcp_server(server<T> *tcp_server); //required to be called to ensure pointer to TCP server is present

  void new_tcp_client(int client_idx);
  void kill_tcp_client(int client_idx);

  void close_connection(int client_idx);

  std::vector<tcp_client> tcp_clients{}; //storing additional data related to the client_idxs passed to this layer
  
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
  cache<3> web_cache{};
  
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

#include "../web_server/web_server.tcc"
#include "../web_server/websockets.tcc"

#endif