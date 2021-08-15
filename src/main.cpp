#include "header/utility.h"
#include "header/web_server/web_server.h"

#include <thread>

int main(){
  signal(SIGINT, utility::sigint_handler); //signal handler for when Ctrl+C is pressed
  signal(SIGPIPE, SIG_IGN); //signal handler for when a connection is closed while writing

  std::cout.setf(std::ios::unitbuf);

  auto &webserver_instance = central_web_server::instance();
  webserver_instance.start_server(".config");

  return 0;
}