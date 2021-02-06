#include "../header/server.h"
#include "../header/utility.h"


/*

Set the fd using wolfSSL to the client index rather than the actual fd

Also maybe you don't need t check if the connection is active here, if you check it's active wherever you call wolfSSL_send or wolfSSL_recv from

*/


int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx){ //send callback, sends a special accept write request to io_uring, and returns how much was written from the send_data map, if appropriate
  int client_idx = wolfSSL_get_fd(ssl);
  auto *tcp_server = (server<server_type::TLS>*)ctx;
  auto &client = tcp_server->clients[client_idx];

  if(tcp_server->active_connections.count(client_idx) && client.send_data.size() > 0){ //as long as the client is definitely active, then send data if there is any
    if(client.send_data.front().last_written == -1){
      tcp_server->add_write_req(client_idx, event_type::WRITE, buff, sz);
      return WOLFSSL_CBIO_ERR_WANT_WRITE;
    }else{
      const auto written = client.send_data.front().last_written;
      client.send_data.front().last_written = -1;
      return written;
    }
  }else{
    if(!client.accept_last_written){
      tcp_server->add_write_req(client_idx, event_type::ACCEPT_WRITE, buff, sz);
      return WOLFSSL_CBIO_ERR_WANT_WRITE;
    }else{
      const auto written = client.accept_last_written;
      client.accept_last_written = 0;
      return written;
    }
  }
}

int tls_recv_helper(server<server_type::TLS> *tcp_server, int client_idx, char *buff, int sz, bool accept){
  auto &client = tcp_server->clients[client_idx];
  auto &data = client.recv_data; //the data vector
  const auto recvd_amount = data.size();

  if(recvd_amount > sz){ //too much
    //if the data needed in this call is less than what we have available,
    //then copy that onto the provided buffer, and return the amount read
    //also then moves the residual data to the beginning properly, dealing with overlaps too
    std::memcpy(buff, &data[0], sz);
    remove_first_n_elements(data, sz);
    return sz;
  }else if(recvd_amount < sz){ //if there isn't enough data available for the full request (too little)
    if(accept)
      tcp_server->add_read_req(client_idx, event_type::ACCEPT_READ);
    return WOLFSSL_CBIO_ERR_WANT_READ; //if there was no data to be read currently, send a request for more data, and respond with this error
  }else{ //just right
    std::memcpy(buff, &data[0], sz); //since this is exactly how much we need, copy the data into the buffer
    client.recv_data = {}; //and erasing the item from the map, since we've used all the data it provided
    return recvd_amount; //sz == recvd_amount in this case
  }
}

int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx){ //receive callback
  int client_idx = wolfSSL_get_fd(ssl);
  auto *tcp_server = (server<server_type::TLS>*)ctx;
  auto &client = tcp_server->clients[client_idx];

  if(tcp_server->active_connections.count(client_idx) && client.recv_data.size() > 0){
    return tls_recv_helper(tcp_server, client_idx, buff, sz, false);
  }else{
    if(client.recv_data.size() > 0){ //if an entry exists in the map, use the data in it, otherwise make a request for it
      return tls_recv_helper(tcp_server, client_idx, buff, sz, true);
    }else{
      // std::cout << "what a surprise 2....\n";
      tcp_server->add_read_req(client_idx, event_type::ACCEPT_READ);
      return WOLFSSL_CBIO_ERR_WANT_READ; //if there was no data to be read currently, send a request for more data, and respond with this error
    } 
  }
}