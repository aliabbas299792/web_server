#ifndef COMMON_STRUCTS_ENUMS
#define COMMON_STRUCTS_ENUMS

#include <string>

struct tcp_client {
  std::string last_requested_read_filepath{}; //the last filepath it was asked to read
  int ws_client_idx{};
  bool using_file = false;
};

#endif