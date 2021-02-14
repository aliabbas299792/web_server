#include "header/callbacks.h"
#include "header/utility.h"

#include <thread>
#include <sys/eventfd.h>

int main(){
  signal(SIGINT, sigint_handler); //signal handler for when Ctrl+C is pressed
  signal(SIGPIPE, SIG_IGN); //signal handler for when a connection is closed while writing

  auto config_data = read_config(); //reads in the config data

  //create the server objects
  if(config_data["TLS"] == "yes"){
    web_server<server_type::TLS> basic_web_server;
    server<server_type::TLS> tcp_server(std::stoi(config_data["TLS_PORT"]), config_data["FULLCHAIN"], config_data["PKEY"], accept_cb<server_type::TLS>, read_cb<server_type::TLS>, write_cb<server_type::TLS>, &basic_web_server); //pass function pointers and a custom object

    std::thread server_thread([&tcp_server](){
      tcp_server.start();
    });

    //should see three "EVENTFD thing messages as a result of these"
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tcp_server.notify_event();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tcp_server.notify_event();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tcp_server.notify_event();
    
    server_thread.join();
  } else {
    web_server<server_type::NON_TLS> basic_web_server;
    server<server_type::NON_TLS> tcp_server(std::stoi(config_data["PORT"]), accept_cb<server_type::NON_TLS>, read_cb<server_type::NON_TLS>, write_cb<server_type::NON_TLS>, &basic_web_server); //pass function pointers and a custom object

    std::thread server_thread([&tcp_server](){
      tcp_server.start();
    });

    //should see three "EVENTFD thing messages as a result of these"
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tcp_server.notify_event();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tcp_server.notify_event();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    tcp_server.notify_event();
    
    server_thread.join();
  }
  
  return 0;
}