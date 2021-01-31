#include "header/callbacks.h"
#include "header/utility.h"

int main(){
  signal(SIGINT, sigint_handler); //signal handler for when Ctrl+C is pressed
  signal(SIGPIPE, SIG_IGN); //signal handler for when a connection is closed while writing

  auto config_data = read_config(); //reads in the config data

  //create the server objects
  web_server basic_web_server;
  server<server_type::NON_TLS> tcp_server(std::stoi(config_data["PORT"]), a_cb<server_type::NON_TLS>, r_cb<server_type::NON_TLS>, w_cb<server_type::NON_TLS>, &basic_web_server); //pass function pointers and a custom object
  //if(config_data["TLS"] == "yes")
    //tcp_server.setup_tls(config_data["FULLCHAIN"], config_data["PKEY"]);
  tcp_server.start();

  return 0;
}