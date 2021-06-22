#ifndef COMMON_STRUCTS_ENUMS
#define COMMON_STRUCTS_ENUMS

#include <string>

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

struct tcp_client {
  std::string last_requested_read_filepath{}; //the last filepath it was asked to read
  int ws_client_idx{};
  bool using_file = false;
};

#endif