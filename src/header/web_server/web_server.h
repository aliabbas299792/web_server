#ifndef BASIC_WEB_SERVER
#define BASIC_WEB_SERVER

#include "../server.h"
#include "../utility.h"
#include "../callbacks.h"
#include "../data_store.h"

#include "../utility.h"

#include "common_structs_enums.h"
#include "cache.h"

#include "../../vendor/readerwriterqueue/atomicops.h"
#include "../../vendor/readerwriterqueue/readerwriterqueue.h"

#include <thread>

#include <openssl/sha.h>
#include <openssl/evp.h>

using uchar = unsigned char;

using namespace web_cache;

const std::string default_plain_text_http_header{"HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\nKeep-Alive: timeout=0, max=0\r\n\r\n"};
const std::string default_plain_json_http_header{"HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nCache-Control: no-cache, no-store, must-revalidate\r\nConnection: close\r\nKeep-Alive: timeout=0, max=0\r\n\r\n"};

namespace web_server{
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
    int client_idx = -1; //for the TCP/TLS layer
  };

  struct message_post_data {
    message_type msg_type{};
    
    const char *buff_ptr{};
    int64_t length = -1;

    std::vector<char> buff{};

    int item_idx = -1;

    int64_t additional_info = -1;
    // for any extra data
    std::string additional_str{};
    std::string additional_str2{};
  };

  struct broadcast_set_data {
    std::unordered_set<int>::iterator begin{};
    std::unordered_set<int>::iterator end{};
    size_t size{};
    broadcast_set_data(std::unordered_set<int> &set){
      begin = set.begin();
      end = set.end();
      size = set.size();
    }
    broadcast_set_data(){}
  };

  struct broadcast_data_items {
    const char* buff_ptr{};
    size_t data_len{};
    size_t uses{};
    broadcast_data_items(const char* buff_ptr = nullptr, int64_t data_len = -1, int64_t uses = -1) : buff_ptr(buff_ptr), data_len(data_len), uses(uses) {}
  };

  template<server_type T>
  class basic_web_server{
    //
    ////generally useful functions and variables
    //
    tcp_tls_server::server<T> *tcp_server = nullptr;

    std::string get_content_type(std::string filepath);

    //
    ////websocket stuff////
    //
    
    //reading data from connections
    ulong get_ws_frame_length(const char *buffer); //helper function which reads the websocket header to get the length of the message
    std::pair<int, std::vector<char>> decode_websocket_frame(std::vector<char> &&data); //decodes a single full websocket frame
    std::pair<int, std::vector<std::vector<char>>> get_ws_frames(char *buffer, int length, int ws_client_idx); //gets any full websocket frames possible
    
    //related to opening/closing connections
    std::string get_accept_header_value(std::string input); //gets the appropriate header value from the websocket connection request
    int new_ws_client(int client_idx); //makes a new websocket client
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

  public:
    static bool instance_exists; // used by anything which needs to be initialised before server threads are made
    
    basic_web_server(basic_web_server &&server) = default;
    basic_web_server() {
      instance_exists = true;
      utility::set_timerfd_interval(ws_ping_timerfd, 30000); // the ping timer should go off every 30 seconds
    };

    void set_tcp_server(tcp_tls_server::server<T> *tcp_server); //required to be called to ensure pointer to TCP server is present

    void new_tcp_client(int client_idx);
    void kill_client(int client_idx);

    void close_connection(int client_idx);

    std::vector<tcp_client> tcp_clients{}; //storing additional data related to the client_idxs passed to this layer

    //thread stuff
    const int central_communication_fd = eventfd(0, 0); // set in main thread
    
    std::vector<broadcast_data_items> broadcast_data{}; // data from any broadcasts sent from the program thread
    std::vector<std::unordered_set<int>> broadcast_ws_clients_tcp_client_idxs{}; // subscribed websocket client idxs are in here
    void subscribe_client(int channel_id, int client_idx){
      if(broadcast_ws_clients_tcp_client_idxs.size() <= channel_id)
        broadcast_ws_clients_tcp_client_idxs.resize(channel_id+1);
      broadcast_ws_clients_tcp_client_idxs[channel_id].insert(client_idx);
    }
    
    broadcast_set_data get_broadcast_set_data(int channel_id){
      if(broadcast_ws_clients_tcp_client_idxs.size() <= channel_id)
        broadcast_ws_clients_tcp_client_idxs.resize(channel_id+1);
      if(channel_id >= 0)
        return broadcast_set_data(broadcast_ws_clients_tcp_client_idxs[channel_id]);
      return broadcast_set_data();
    }
    
    void post_message_to_server_thread(message_type msg_type, const char *buff_ptr, size_t length, int item_idx, uint64_t additional_info = -1){ //called from the program thread, to notify the server thread
      if(!tcp_server) return; // need this set before posting any messages
      message_post_data data;
      data.msg_type = msg_type;
      data.buff_ptr = buff_ptr;
      data.length = length;
      data.item_idx = item_idx;
      data.additional_info = additional_info;
      to_server_queue.enqueue(std::move(data));
      // std::cout << "size to server: \e[34m" << to_server_queue.size_approx() << "\e[0m" << std::endl;
      tcp_server->notify_event();
    }

    void post_memory_release_message_to_program(message_type msg_type, const char *buff_ptr, size_t length, int item_idx, uint64_t additional_info = -1){
      if(!tcp_server) return; // need this stuff set before posting any messages
      message_post_data data;
      data.msg_type = msg_type;
      data.buff_ptr = buff_ptr;
      data.length = length;
      data.item_idx = item_idx;
      data.additional_info = additional_info;
      to_program_queue.enqueue(std::move(data));

      eventfd_write(central_communication_fd, 1); //notify the program thread using our eventfd
    }
    
    message_post_data get_from_to_program_queue(){ // so called from main program thread
      message_post_data data{};
      to_program_queue.try_dequeue(data);
      return data;
    }

    message_post_data get_from_to_server_queue(){ // so called from associated server thread
      message_post_data data{};
      to_server_queue.try_dequeue(data);
      return data;
    }

    //
    ////http public methods
    //

    //responding to get requests
    bool get_process(std::string &path, bool accept_bytes, const std::string& sec_websocket_key, int client_idx, std::string ip = {});
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
    static std::vector<char> make_ws_frame(const std::string &packet_msg, websocket_non_control_opcodes opcode);
    
    bool close_ws_connection_req(int ws_client_idx, bool client_already_closed = false); //puts in a request to close this websocket connection

    void websocket_process_read_cb(int client_idx, char *buffer, int length);
    bool websocket_process_write_cb(int client_idx); //returns whether or not this was used
    void websocket_accept_read_cb(const std::string& sec_websocket_key, const std::string &path, int client_idx); //used in the read callback to accept web sockets

    //writing data to connections
    void websocket_write(int ws_client_idx, std::vector<char> &&buff);

    int get_ws_client_tcp_client_idx(int ws_client_idx, int ws_client_id){
      auto &ws_client = websocket_clients[ws_client_idx];
      if(websocket_clients[ws_client_idx].id == ws_client_id)
        return ws_client.client_idx;
      return -1;
    }

    void ping_all_websockets(){
      if(tcp_server && websocket_clients.size()){
        static std::vector<char> ping_data = make_ws_frame("", websocket_non_control_opcodes::ping);

        tcp_server->broadcast_message(
          active_websocket_connections_client_idxs.begin(),
          active_websocket_connections_client_idxs.end(),
          active_websocket_connections_client_idxs.size(),
          ping_data.data(),
          ping_data.size()
        ); // send the ping message
      }
    }

    const int ws_ping_timerfd = timerfd_create(CLOCK_MONOTONIC, 0); // used for pinging ws connections

    //websocket data
    std::unordered_set<int> all_websocket_connections{}; //this is used for the duration of the connection (even after we've sent the close request)
    std::unordered_set<int> active_websocket_connections_client_idxs{}; //this is only active up until we call a close request, has client_idx

    ~basic_web_server(){
      close(web_cache.inotify_fd);
    }
  };

  #include "../../web_server/web_server.tcc"
  #include "../../web_server/websockets.tcc"
}

enum class central_web_server_event { TIMERFD, READ, WRITE, SERVER_THREAD_COMMUNICATION, KILL_SERVER };

struct central_web_server_req {
  central_web_server_event event{};
  std::vector<char> buff{};
  
  const char *buff_ptr{};
  size_t size = -1; // if this is -1, buff_ptr is unused

  size_t progress_bytes{}; // how much has been read/written
  int fd = -1;

  uint64_t custom_info = -1;
};

class central_web_server {
private:
  template<server_type T>
  friend struct server_data;

  static std::unordered_map<std::string, std::string> config_data_map;

  template<server_type T>
  static void thread_server_runner(web_server::basic_web_server<T> &basic_web_server);

  central_web_server() {};

  void run();

  template<server_type T>
  void run();

  int num_threads = -1;

  const int event_fd = eventfd(0, 0);
  const int kill_server_efd = eventfd(0, 0);
  
  io_uring ring;

  data_store_namespace::data_store store{}; // the data store

  void add_timer_read_req(int timerfd); // adds io_uring read request for the timerfd
  void add_read_req(int fd, size_t size, int custom_info = -1); // adds normal read request on io_uring
  void add_write_req(int fd, const char *buff_ptr, size_t size); // adds normal write request on io_uring

  // to finish off the requests
  void read_req_continued(central_web_server_req *req, size_t last_read);
  void write_req_continued(central_web_server_req *req, size_t written);
  
public:
  void start_server(const char *config_file_path);
  void add_event_read_req(int eventfd, central_web_server_event event, uint64_t custom_info = -1); // adds io_uring read request for the eventfd

  central_web_server(central_web_server const&) = delete;
  void operator=(central_web_server const&) = delete;

  static central_web_server& instance(){
    static central_web_server inst;
    return inst;
  }
  
  void kill_server();
};

template<server_type T>
struct server_data {
  std::thread thread{};
  web_server::basic_web_server<T> server{};
  server_data(){
    thread = std::thread(central_web_server::thread_server_runner<T>, std::ref(server));
  }
  server_data(server_data &&data) = default;
  ~server_data() {
    thread.join();
  }
};

#endif