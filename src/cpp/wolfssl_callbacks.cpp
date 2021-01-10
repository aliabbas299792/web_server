#include "../header/server.h"

int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx){ //send callback, sends a special accept write request to io_uring, make it not always do that
  int client_socket = ((rw_cb_context*)ctx)->client_socket;
  auto *tcp_server = ((rw_cb_context*)ctx)->tcp_server;

  if(wolfSSL_get_fd(ssl) != client_socket) return WOLFSSL_CBIO_ERR_CONN_CLOSE;

  if(tcp_server->active_connections.count(client_socket) && tcp_server->send_data[client_socket].size() > 0){
    if(tcp_server->send_data[client_socket].front().last_written == -1){
      tcp_server->add_write_req(client_socket, buff, sz);
      return WOLFSSL_CBIO_ERR_WANT_WRITE;
    }else{
      const auto written = tcp_server->send_data[client_socket].front().last_written;
      tcp_server->send_data[client_socket].front().last_written = -1;
      return written;
    }
  }else{
    if(!tcp_server->accept_send_data.count(client_socket)){
      tcp_server->add_write_req(client_socket, buff, sz, true);
      return WOLFSSL_CBIO_ERR_WANT_WRITE;
    }else{
      const auto written = tcp_server->accept_send_data[client_socket];
      tcp_server->accept_send_data.erase(client_socket);
      return written;
    }
  }
}

int tls_recv_helper(std::unordered_map<int, std::pair<char*, int>> *accept_recv_data, server *tcp_server, char *buff, int sz, int client_socket, bool accept){
  auto *data = &(*accept_recv_data)[client_socket];
  const auto all_received = data->first; //we don't free this buffer in this function, since it's taken care of in the io_uring loop
  const auto recvd_amount = data->second;

  if(recvd_amount > sz){ //too much
    //if the data needed in this call is less than what we have available,
    //then copy that onto the provided buffer, and return the amount read
    std::memcpy(buff, all_received, sz);
    *data = { (char*)std::malloc(recvd_amount - sz), recvd_amount - sz };
    std::memset(data->first, 0, recvd_amount - sz);
    std::memcpy(data->first, &all_received[sz], recvd_amount - sz);
    return sz;
  }else if(recvd_amount < sz){ //in the off chance that there isn't enough data available for the full request (too little)
    if(accept)
      tcp_server->add_read_req(client_socket, true);
    else
      tcp_server->add_read_req(client_socket, false);
    return WOLFSSL_CBIO_ERR_WANT_READ; //if there was no data to be read currently, send a request for more data, and respond with this error
  }else{ //just right
    std::memcpy(buff, all_received, sz); //since this is exactly how much we need, copy the data into the buffer
    free(data->first);
    accept_recv_data->erase(client_socket); //and erasing the item from the map, since we've used all the data it provided
    return recvd_amount; //sz == recvd_amount in this case
  }
}

int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx){ //receive callback
  uint client_socket = ((rw_cb_context*)ctx)->client_socket;
  auto *tcp_server = ((rw_cb_context*)ctx)->tcp_server;
  auto *accept_recv_data = &tcp_server->accept_recv_data;

  if(wolfSSL_get_fd(ssl) != client_socket) return WOLFSSL_CBIO_ERR_CONN_CLOSE;

  std::cout << "socket: " << client_socket << " | tcp_server address: " << tcp_server << "\n";
  std::cout << tcp_server->active_connections.bucket_count() << "\n";
  if(tcp_server->active_connections.count(client_socket) && accept_recv_data->count(client_socket)){
    return tls_recv_helper(accept_recv_data, tcp_server, buff, sz, client_socket, false);
  }else{
    if(accept_recv_data->count(client_socket)){ //if an entry exists in the map, use the data in it, otherwise make a request for it
      return tls_recv_helper(accept_recv_data, tcp_server, buff, sz, client_socket, true);
    }else{
      tcp_server->add_read_req(client_socket, true);
      return WOLFSSL_CBIO_ERR_WANT_READ; //if there was no data to be read currently, send a request for more data, and respond with this error
    } 
  }
}