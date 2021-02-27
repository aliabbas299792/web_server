#include "../header/web_server/central_web_server.h"
#include "../header/callbacks.h"
#include "../header/web_server/web_server.h"

void central_web_server::tls_thread_server_runner(int thread_tls_web_servers_ptr_idx){
  tls_web_server basic_web_server{};
  thread_tls_web_servers_ptr[thread_tls_web_servers_ptr_idx] = &basic_web_server;

  tls_server tcp_server(
    std::stoi(config_data_map["TLS_PORT"]),
    config_data_map["FULLCHAIN"],
    config_data_map["PKEY"],
    &basic_web_server,
    accept_cb<server_type::TLS>,
    close_cb<server_type::TLS>,
    read_cb<server_type::TLS>,
    write_cb<server_type::TLS>,
    event_cb<server_type::TLS>,
    custom_read_cb<server_type::TLS>
  ); //pass function pointers and a custom object

  basic_web_server.set_tcp_server(&tcp_server); //required to be called, to give it a pointer to the server
  
  tcp_server.start();
}

void central_web_server::plain_thread_server_runner(int thread_plain_web_servers_ptr_idx){
  plain_web_server basic_web_server{};
  thread_plain_web_servers_ptr[thread_plain_web_servers_ptr_idx] = &basic_web_server;

  plain_server tcp_server(
    std::stoi(config_data_map["PORT"]),
    &basic_web_server,
    accept_cb<server_type::NON_TLS>,
    close_cb<server_type::NON_TLS>,
    read_cb<server_type::NON_TLS>,
    write_cb<server_type::NON_TLS>,
    event_cb<server_type::NON_TLS>,
    custom_read_cb<server_type::NON_TLS>
  ); //pass function pointers and a custom object
  
  basic_web_server.set_tcp_server(&tcp_server); //required to be called, to give it a pointer to the server
  
  tcp_server.start();
}