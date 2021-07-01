#include "header/utility.h"
#include "header/web_server/web_server.h"

#include <thread>

// uint64_t mem_usage_event = 0;
// uint64_t mem_usage_read = 0;
// uint64_t mem_usage_write = 0;
// uint64_t mem_usage_customread = 0;
// uint64_t mem_usage_accept = 0;

int main(){
  signal(SIGINT, utility::sigint_handler); //signal handler for when Ctrl+C is pressed
  signal(SIGPIPE, SIG_IGN); //signal handler for when a connection is closed while writing

  auto &webserver_instance = central_web_server::instance();
  webserver_instance.start_server(".config");

  return 0;
}