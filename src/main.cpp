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
    tls_web_server basic_web_server;
    tls_server tcp_server(
      std::stoi(config_data["TLS_PORT"]),
      config_data["FULLCHAIN"],
      config_data["PKEY"],
      &basic_web_server,
      accept_cb<server_type::TLS>,
      read_cb<server_type::TLS>,
      write_cb<server_type::TLS>,
      event_cb<server_type::TLS>,
      custom_read_cb<server_type::TLS>
    ); //pass function pointers and a custom object

    basic_web_server.set_tcp_server(&tcp_server); //required to be called, to give it a pointer to the server

    std::thread server_thread([&tcp_server](){
      tcp_server.start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    tcp_server.notify_event();
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    tcp_server.notify_event();
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    tcp_server.notify_event();
    
    server_thread.join();
  } else {
    plain_web_server basic_web_server;

    plain_server tcp_server(
      std::stoi(config_data["PORT"]),
      &basic_web_server,
      accept_cb<server_type::NON_TLS>,
      read_cb<server_type::NON_TLS>,
      write_cb<server_type::NON_TLS>,
      event_cb<server_type::NON_TLS>,
      custom_read_cb<server_type::NON_TLS>
    ); //pass function pointers and a custom object
    
    basic_web_server.set_tcp_server(&tcp_server); //required to be called, to give it a pointer to the server

    std::thread server_thread([&tcp_server](){
      tcp_server.start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    tcp_server.notify_event();
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    tcp_server.notify_event();
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    tcp_server.notify_event();
    
    server_thread.join();
  }
  
  return 0;
}