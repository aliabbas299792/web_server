#include "header/callbacks.h"
#include "header/utility.h"

int main(){
  signal(SIGINT, sigint_handler); //signal handler for when Ctrl+C is pressed
  signal(SIGPIPE, SIG_IGN); //signal handler for when a connection is closed while writing

  auto config_data = read_config(); //reads in the config data

  //create the server objects
  if(config_data["TLS"] == "yes"){
    web_server<server_type::TLS> basic_web_server;
    server<server_type::TLS> tcp_server(std::stoi(config_data["TLS_PORT"]), config_data["FULLCHAIN"], config_data["PKEY"], a_cb<server_type::TLS>, r_cb<server_type::TLS>, w_cb<server_type::TLS>, &basic_web_server); //pass function pointers and a custom object
    tcp_server.start();
  } else {
    web_server<server_type::NON_TLS> basic_web_server;
    server<server_type::NON_TLS> tcp_server(std::stoi(config_data["PORT"]), a_cb<server_type::NON_TLS>, r_cb<server_type::NON_TLS>, w_cb<server_type::NON_TLS>, &basic_web_server); //pass function pointers and a custom object
    tcp_server.start();
  }
  

  return 0;
}