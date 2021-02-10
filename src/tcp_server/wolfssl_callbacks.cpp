#include "../header/server.h"
#include "../header/utility.h"

int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx){ //send callback, sends a special accept write request to io_uring, and returns how much was written from the send_data map, if appropriate
  int client_idx = wolfSSL_get_fd(ssl);
  auto *tcp_server = (server<server_type::TLS>*)ctx;
  auto &client = tcp_server->clients[client_idx];
  // std::cout << "WANT TO WRITE: " << sz << " ## LAST WROTE: " << client.accept_last_written << "\n";

  if(tcp_server->active_connections.count(client_idx)){ //as long as the client is definitely active
    auto &current = client.send_data.front();
    if(current.last_written == -1){
      tcp_server->add_write_req(client_idx, event_type::WRITE, buff, sz);
      return WOLFSSL_CBIO_ERR_WANT_WRITE;
    }else{
      const auto written = current.last_written;
      current.last_written = -1;
      return written;
    }
  }else{
    if(client.accept_last_written == -1){
      tcp_server->add_write_req(client_idx, event_type::ACCEPT_WRITE, buff, sz);
      return WOLFSSL_CBIO_ERR_WANT_WRITE;
    }else{
      const auto written = client.accept_last_written;
      client.accept_last_written = -1;
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
    if(accept) //we only send read requests via wolfSSL for the TLS negotiation bit
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

  if(tcp_server->active_connections.count(client_idx)){ //only active once TLS negotiations are finished
    if(client.recv_data.size()) //if the amount to send is non-zero, then we can return however much we read
      return tls_recv_helper(tcp_server, client_idx, buff, sz, false);

    tcp_server->add_read_req(client_idx, event_type::READ); //otherwise we've gotta read stuff
    return WOLFSSL_CBIO_ERR_WANT_READ;
  }else{
    if(client.recv_data.size() > 0){ //if an entry exists in the map, use the data in it, otherwise make a request for it
      return tls_recv_helper(tcp_server, client_idx, buff, sz, true);
    }else{
      tcp_server->add_read_req(client_idx, event_type::ACCEPT_READ);
      return WOLFSSL_CBIO_ERR_WANT_READ; //if there was no data to be read currently, send a request for more data, and respond with this error
    }
  }
}