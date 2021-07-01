#ifndef TCP_SERVER_CALLBACKS
#define TCP_SERVER_CALLBACKS

#include "server_metadata.h"
#include "web_server/common_structs_enums.h"

#include <sys/inotify.h>

#include <openssl/sha.h>
#include <openssl/evp.h>

using uchar = unsigned char;

namespace tcp_callbacks {
  
  template<server_type T>
  void accept_cb(ACCEPT_CB_PARAMS);

  template<server_type T>
  void close_cb(CLOSE_CB_PARAMS);

  template<server_type T>
  void read_cb(READ_CB_PARAMS);

  template<server_type T>
  void write_cb(WRITE_CB_PARAMS);

  template<server_type T>
  void event_cb(EVENT_CB_PARAMS);

  template<server_type T>
  void custom_read_cb(CUSTOM_READ_CB_PARAMS);

  #include "../web_server/callbacks.tcc" //template implementation file
}

#endif