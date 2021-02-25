#ifndef WEB_SERVER
#define WEB_SERVER

#include "../server.h"
#include "../utility.h"
#include "../callbacks.h"

#include "common_structs_enums.h"
#include "cache.h"

#include "../../vendor/readerwriterqueue/atomicops.h"
#include "../../vendor/readerwriterqueue/readerwriterqueue.h"

#include <thread>

using uchar = unsigned char;

using tls_server = server<server_type::TLS>;
using plain_server = server<server_type::NON_TLS>;
using tls_web_server = web_server<server_type::TLS>;
using plain_web_server = web_server<server_type::NON_TLS>;

enum websocket_non_control_opcodes {
  text_frame = 0x01,
  binary_frame = 0x02,
  close_connection = 0x08,
  ping = 0x09,
  pong = 0xA
};

enum class message_type {
  websocket_broadcast,
  broadcast_finished
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

struct message_post_data {
  message_post_data(message_type msg_type, const char *buff_ptr, size_t length, uint64_t additional_info) : msg_type(msg_type), buff_ptr(buff_ptr), length(length), additional_info(additional_info) {}
  message_type msg_type;
  const char *buff_ptr;
  size_t length;
  uint64_t additional_info;
};



template<server_type T>
class web_server{
  //
  ////generally useful functions and variables
  //
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
  moodycamel::ReaderWriterQueue<message_post_data> to_server_queue{};
  moodycamel::ReaderWriterQueue<message_post_data> to_program_queue{};

  int central_eventfd{};
public:
  web_server() {};

  void set_tcp_server(server<T> *tcp_server); //required to be called to ensure pointer to TCP server is present

  void new_tcp_client(int client_idx);
  void kill_tcp_client(int client_idx);

  void close_connection(int client_idx);

  std::vector<tcp_client> tcp_clients{}; //storing additional data related to the client_idxs passed to this layer

  //thread stuff
  void set_central_eventfd(int efd){
    this->central_eventfd = efd; //sets the program efd
  }

  void post_message_to_server_thread(message_type msg_type, const char *buff_ptr, size_t length, uint64_t additional_info){ //called from the program thread, to notify the server thread
    to_server_queue.emplace(msg_type, buff_ptr, length, additional_info);
    tcp_server->notify_event();
  }

  void post_message_to_program(message_type msg_type, const char *buff_ptr, size_t length, uint64_t additional_info){
    to_program_queue.emplace(msg_type, buff_ptr, length, additional_info);
    eventfd_write(central_eventfd, 1); //notify the program thread
  }
  
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





class central_web_server {
private:
  std::unordered_map<char*, int> buff_ptr_to_uses_map{};
  std::vector<std::thread> thread_container{};

  static std::unordered_map<std::string, std::string> config_data_map;

  static void tls_thread_server_runner();
  static void plain_thread_server_runner();
public:
  central_web_server(const char *config_file_path){
    auto file_fd = open(config_file_path, O_RDONLY);
    if(file_fd == -1)
      fatal_error("Ensure the .config file is in this directory");
    auto file_size = get_file_size(file_fd);
    
    std::vector<char> config(file_size+1);
    int read_amount = 0;
    while(read_amount != file_size)
      read_amount += read(file_fd, &config[0], file_size - read_amount);
    config[read_amount] = '\0';  //sets the final byte to NULL so that strtok_r stops there

    close(file_fd);
    
    std::vector<std::vector<char>> lines;
    char *begin_ptr = &config[0];
    char *line = nullptr;
    char *saveptr = nullptr;
    while((line = strtok_r(begin_ptr, "\n", &saveptr))){
      begin_ptr = nullptr;
      lines.emplace(lines.end(), line, line + strlen(line));
    }
    
    for(auto line : lines){
      int shrink_by = 0;
      const auto length = line.size();
      for(int i = 0; i < length; i++){ //removes whitespace
        if(line[i] ==  ' ')
          shrink_by++;
        else
          line[i-shrink_by] = line[i];
      }
      if(shrink_by)
        line[length-shrink_by] = 0; //sets the byte immediately after the last content byte to NULL so that strtok_r stops there
      if(line[0] == '#') continue; //this is a comment line, so ignore it
      char *saveptr = nullptr;
      std::string key = strtok_r(&line[0], ":", &saveptr);
      std::string value = strtok_r(nullptr, ":", &saveptr);
      config_data_map[key] = value;    
    }

    if(config_data_map.count("TLS") && config_data_map["TLS"] == "yes"){
      if(!config_data_map.count("FULLCHAIN") || !config_data_map.count("PKEY") || !config_data_map.count("TLS_PORT"))
        fatal_error("Please provide FULLCHAIN, PKEY and TLS_PORT settings in the config file");
    }else if(!config_data_map.count("PORT")){
      fatal_error("Please provide the PORT setting in the config file");
    }

    //done reading config
    const auto num_threads = config_data_map.count("SERVER_THREADS") ? std::stoi(config_data_map["SERVER_THREADS"]) : 3; //by default uses 3 threads

    std::cout << "Using " << num_threads << " threads\n";

    for(int i = 0; i < num_threads; i++){
      if(config_data_map["TLS"] == "yes")
        thread_container.push_back(std::thread(tls_thread_server_runner));
      else
        thread_container.push_back(std::thread(plain_thread_server_runner));
    }
    
    while(true){
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
  }
};

void central_web_server::tls_thread_server_runner(){
  tls_web_server basic_web_server;
  tls_server tcp_server(
    std::stoi(config_data_map["TLS_PORT"]),
    config_data_map["FULLCHAIN"],
    config_data_map["PKEY"],
    &basic_web_server,
    accept_cb<server_type::TLS>,
    close_cb<server_type::TLS>,
    read_cb<server_type::TLS>,
    write_cb<server_type::TLS>,
    event_cb<server_type::TLS>,
    custom_read_cb<server_type::TLS>
  ); //pass function pointers and a custom object

  basic_web_server.set_tcp_server(&tcp_server); //required to be called, to give it a pointer to the server
  
  tcp_server.start();
}

void central_web_server::plain_thread_server_runner(){
  plain_web_server basic_web_server;

  plain_server tcp_server(
    std::stoi(config_data_map["PORT"]),
    &basic_web_server,
    accept_cb<server_type::NON_TLS>,
    close_cb<server_type::NON_TLS>,
    read_cb<server_type::NON_TLS>,
    write_cb<server_type::NON_TLS>,
    event_cb<server_type::NON_TLS>,
    custom_read_cb<server_type::NON_TLS>
  ); //pass function pointers and a custom object
  
  basic_web_server.set_tcp_server(&tcp_server); //required to be called, to give it a pointer to the server
  
  tcp_server.start();
}

std::unordered_map<std::string, std::string> central_web_server::config_data_map{};









#include "../../web_server/web_server.tcc"
#include "../../web_server/websockets.tcc"

#endif