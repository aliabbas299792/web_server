#include "header/callbacks.h"
#include "header/utility.h"

int main(){
  signal(SIGINT, sigint_handler); //signal handler for when Ctrl+C is pressed
  signal(SIGPIPE, SIG_IGN); //signal handler for when a connection is closed while writing

  auto config_data = read_config(); //reads in the config data

  //create the server objects
  web_server basic_web_server;
  server tcp_server(std::stoi(config_data["PORT"]), a_cb, r_cb, w_cb, &basic_web_server); //pass function pointers and a custom object
  if(config_data["TLS"] == "yes")
    tcp_server.setup_tls(config_data["FULLCHAIN"], config_data["PKEY"]);
  tcp_server.start();

  return 0;
}