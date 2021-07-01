#ifndef SERVER
#define SERVER

#include <cstring> //for memset and strtok

#include <stdio.h> //perror and printf
#include <netdb.h> //for networking stuff like addrinfo

#include <sys/syscall.h> //syscall stuff parameters (as in like __NR_io_uring_enter/__NR_io_uring_setup)
#include <sys/mman.h> //for mmap
#include <sys/eventfd.h> // for eventfd
#include <sys/timerfd.h> // for timerfd

#include <liburing.h> //for liburing

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

#include <queue>
#include <iostream> //for string and iostream stuff
#include <unordered_map>
#include <unordered_set>
#include <set> //ordered set for freed indexes, I believe it is sorted in ascending order which is exactly what we want
#include <chrono>
#include <mutex>

#include "server_metadata.h"
#include "utility.h"

namespace tcp_tls_server {
  //the wolfSSL callbacks
  int tls_recv_helper(server<server_type::TLS> *tcp_server, int client_idx, char *buff, int sz, bool accept);
  int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx);
  int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx);

  template<server_type T>
  using accept_callback = void (*)(ACCEPT_CB_PARAMS);

  template<server_type T>
  using close_callback = void (*)(CLOSE_CB_PARAMS);

  template<server_type T>
  using read_callback = void(*)(READ_CB_PARAMS);

  template<server_type T>
  using write_callback = void(*)(WRITE_CB_PARAMS);

  template<server_type T>
  using event_callback = void(*)(EVENT_CB_PARAMS);

  template<server_type T>
  using custom_read_callback = void(*)(CUSTOM_READ_CB_PARAMS);

  // extern uint64_t mem_usage_event;
  // extern uint64_t mem_usage_read;
  // extern uint64_t mem_usage_write;
  // extern uint64_t mem_usage_customread;
  // extern uint64_t mem_usage_accept;

  struct request {
    // fields used for any request
    event_type event;
    int client_idx = -1;
    int ID = -1;

    // fields used for write requests
    size_t written{}; //how much written so far
    size_t total_length{}; //how much data is in the request, in bytes
    const char *buffer = nullptr;

    // fields used for read requests
    std::vector<char> read_data{};
    size_t read_amount{}; //how much has been read (in case of multi read requests)
    
    // extra
    int64_t custom_info{}; //any custom info you want to attach to the request
  };

  struct multi_write {
    multi_write(std::vector<char> &&buff, int uses) : buff(std::move(buff)), uses(uses) {}
    std::vector<char> buff;
    int uses{}; //this should be decremented each time you would normally delete this object, when it reaches 0, then delete
  };

  struct write_data { //this is closer to 3 objects in 1
    int last_written = -1;

    int64_t custom_info{};

    write_data(std::vector<char> &&buff, uint64_t custom_info = 0) : buff(buff), custom_info(custom_info) {}
    std::vector<char> buff;

    write_data(const char *buff, size_t length, bool broadcast = false, uint64_t custom_info = 0) : ptr_buff(buff), total_length(length), broadcast(broadcast), custom_info(custom_info) {}
    bool broadcast = false; // if true, then do something appropriate
    const char *ptr_buff = nullptr; //in the case you only want to write a char* ptr - this basically trusts that you won't invalidate the pointer
    size_t total_length{}; //used in conjunction with the above

    write_data(multi_write *multi_write_data, uint64_t custom_info = 0) : multi_write_data(multi_write_data), custom_info(custom_info) {}
    multi_write *multi_write_data = nullptr; //if not null then buff should be empty, and data should be in the multi_write pointer
    
    ~write_data(){
      if(multi_write_data){
        multi_write_data->uses--;
        if(multi_write_data->uses == 0)
          delete multi_write_data;
      }
    }

    struct ptr_and_size {
      ptr_and_size(const char *buff, size_t length) : buff(buff), length(length) {}
      const char *buff = nullptr;
      size_t length{};
    };
    
    ptr_and_size get_ptr_and_size(){
      if(multi_write_data){
        return { &(multi_write_data->buff[0]), multi_write_data->buff.size() };
      }else if(ptr_buff){
        return { ptr_buff, total_length };
      }else{
        return { &buff[0], buff.size() };
      }
    }
  };

  struct client_base {
    int id{};
    int sockfd{};
    std::queue<write_data> send_data{};

    bool read_req_active = false;
    int num_write_reqs = 0; // if this is non zero, then do not proceed with the close callback, wait for other requests to finish
  };

  template<server_type T>
  struct client: client_base {};

  template<>
  struct client<server_type::NON_TLS>: client_base {};

  template<>
  struct client<server_type::TLS>: client_base {
      WOLFSSL *ssl = nullptr;
      int accept_last_written = -1;
      std::vector<char> recv_data{};
  };

  template<server_type T>
  class server_base {
    protected:
      accept_callback<T> accept_cb = nullptr;
      close_callback<T> close_cb = nullptr;
      read_callback<T> read_cb = nullptr;
      write_callback<T> write_cb = nullptr;
      event_callback<T> event_cb = nullptr;
      custom_read_callback<T> custom_read_cb = nullptr;

      io_uring ring;
      void *custom_obj; //it can be anything

      std::unordered_set<int> active_connections{};
      std::set<int> freed_indexes{}; //using a set to store free indexes instead
      std::vector<client<T>> clients{};

      int timerfd = timerfd_create(CLOCK_MONOTONIC, 0); // used for pinging connections

      void add_tcp_accept_req();

      //need it protected rather than private, since need to access from children
      int add_write_req(int client_idx, event_type event, const char *buffer, unsigned int length); //this is for the case you want to write a buffer rather than a vector
      //used internally for sending messages
      int add_read_req(int client_idx, event_type event); //adds a read request to the io_uring ring
      //arms the timerfd
      void add_timerfd_read_req();

      void custom_read_req_continued(request *req, size_t last_read); //to finish off partial reads
      
      int setup_client(int client_idx);

      void event_read(int event_fd, event_type event); //will set a read request for the eventfd

      bool ran_server = false;
    private:
      int notification_efd = eventfd(0, 0); //used to awaken this thread for some event
      int kill_efd = eventfd(0, 0); //used to awaken this thread to be killed

      int listener_fd = 0;

      int add_accept_req(int listener_fd, sockaddr_storage *client_address, socklen_t *client_address_length); //adds an accept request to the io_uring ring
      //used in the req_event_handler functions for accept requests
      sockaddr_storage client_address{};
      socklen_t client_address_length = sizeof(client_address);

      int setup_listener(int port); //sets up the listener socket

      //needed to synchronize the multiple server threads
      static std::mutex init_mutex;
      static int shared_ring_fd; //pointer to a single io_uring ring fd, who's async backend is shared
    public:
      server_base(int listen_port);
      void start(); //function to start the server

      void read_connection(int client_idx);

      //to read for a custom fd and be notified via the CUSTOM_READ event
      void custom_read_req(int fd, size_t to_read, int client_idx = -1, std::vector<char> &&buff = {}, size_t read_amount = 0);

      void notify_event();
      void kill_server(); // will kill the server

      bool is_active = true; // is the server active (only false once it received an exit signal)
  };

  template<>
  class server<server_type::NON_TLS>: public server_base<server_type::NON_TLS> {
    private:
      friend class server_base;

      //this takes the request pointer by reference, since for now, we are still using some manual memory management
      void req_event_handler(request *&req, int cqe_res); //the main event handler

      int add_write_req_continued(request *req, int offset); //only used for when writev didn't write everything
      
      // for storing and accessing all of the non TLS servers on all threads
      static std::vector<server<server_type::NON_TLS>*> non_tls_servers;
      static std::mutex non_tls_server_vector_access;
    public:
      server(int listen_port,
        void *custom_obj = nullptr,
        accept_callback<server_type::NON_TLS> a_cb = nullptr,
        close_callback<server_type::NON_TLS> c_cb = nullptr,
        read_callback<server_type::NON_TLS> r_cb = nullptr,
        write_callback<server_type::NON_TLS> w_cb = nullptr,
        event_callback<server_type::NON_TLS> e_cb = nullptr,
        custom_read_callback<server_type::NON_TLS> cr_cb = nullptr
      );

      template<typename U>
      void broadcast_message(U begin, U end, int num_clients, std::vector<char> &&buff){
        if(num_clients > 0){
          auto data = new multi_write(std::move(buff), num_clients);

          for(auto client_idx_ptr = begin; client_idx_ptr != end; client_idx_ptr++){
            auto &client = clients[(int)*client_idx_ptr];
            client.send_data.emplace(data);
            if(client.send_data.size() == 1) //only adds a write request in the case that the queue was empty before this
              add_write_req(*client_idx_ptr, event_type::WRITE, &(data->buff[0]), data->buff.size());
          }
        }
      }

      template<typename U>
      void broadcast_message(U begin, U end, int num_clients, const char *buff, size_t length, uint64_t custom_info = 0){ //if the buff pointer is ever invalidated, it will just fail to write - so sort of unsafe on its own
        if(num_clients > 0){
          for(auto client_idx_ptr = begin; client_idx_ptr != end; client_idx_ptr++){
            auto &client = clients[(int)*client_idx_ptr];
            client.send_data.emplace(buff, length, true, custom_info);
            if(client.send_data.size() == 1) //only adds a write request in the case that the queue was empty before this
              add_write_req(*client_idx_ptr, event_type::WRITE, buff, length);
          }
        }
      }

      static void kill_all_servers(); // will kill all non tls servers on any thread

      void write_connection(int client_idx, std::vector<char> &&buff); //writing depends on TLS or SSL, unlike read
      void write_connection(int client_idx, char *buff, size_t length); //writing but using a char pointer, doesn't do anything to the data
      void close_connection(int client_idx); //closing depends on what resources need to be freed
  };

  template<>
  class server<server_type::TLS>: public server_base<server_type::TLS> {
    private:
      friend int tls_recv_helper(server<server_type::TLS> *tcp_server, int client_idx, char *buff, int sz, bool accept);
      friend int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx);
      friend int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx);

      friend class server_base;
      void tls_accept(int client_socket);
      
      //this takes the request pointer by reference, since for now, we are still using some manual memory management
      void req_event_handler(request *&req, int cqe_res); //the main event handler

      WOLFSSL_CTX *wolfssl_ctx = nullptr;

      // for storing and accessing all of the TLS servers on all threads
      static std::vector<server<server_type::TLS>*> tls_servers;
      static std::mutex tls_server_vector_access;
    public:
      server(
        int listen_port,
        std::string fullchain_location,
        std::string pkey_location,
        void *custom_obj = nullptr,
        accept_callback<server_type::TLS> a_cb = nullptr,
        close_callback<server_type::TLS> c_cb = nullptr,
        read_callback<server_type::TLS> r_cb = nullptr,
        write_callback<server_type::TLS> w_cb = nullptr,
        event_callback<server_type::TLS> e_cb = nullptr,
        custom_read_callback<server_type::TLS> cr_cb = nullptr
      );
      
      template<typename U>
      void broadcast_message(U begin, U end, int num_clients, std::vector<char> &&buff){
        if(num_clients > 0){
          auto data = new multi_write(std::move(buff), num_clients);

          for(auto client_idx_ptr = begin; client_idx_ptr != end; client_idx_ptr++){
            auto &client = clients[(int)*client_idx_ptr];
            client.send_data.emplace(data);
            if(client.send_data.size() == 1) //only adds a write request in the case that the queue was empty before this
              wolfSSL_write(client.ssl, &(data->buff[0]), data->buff.size());
          }
        }
      }

      template<typename U>
      void broadcast_message(U begin, U end, int num_clients, const char *buff, size_t length, uint64_t custom_info = 0){ //if the buff pointer is ever invalidated, it will just fail to write - so sort of unsafe on its own
        if(num_clients > 0){
          for(auto client_idx_ptr = begin; client_idx_ptr != end; client_idx_ptr++){
            auto &client = clients[(int)*client_idx_ptr];
            client.send_data.emplace(buff, length, true, custom_info);
            if(client.send_data.size() == 1) //only adds a write request in the case that the queue was empty before this
              wolfSSL_write(client.ssl, buff, length);
          }
        }
      }

      static void kill_all_servers(); // will kill all tls servers on any thread

      void write_connection(int client_idx, std::vector<char> &&buff); //writing depends on TLS or SSL, unlike read
      void write_connection(int client_idx, char *buff, size_t length); //writing but using a char pointer, doesn't do anything to the data
      void close_connection(int client_idx); //closing depends on what resources need to be freed
  };

  #include "../tcp_server/server_base.tcc" //template implementation file
}

#endif