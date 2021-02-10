#include "../header/server.h"
#include "../header/utility.h"

void server<server_type::TLS>::close_connection(int client_idx) {
  auto &client_ = clients[client_idx];
  wolfSSL_shutdown(client_.ssl);
  wolfSSL_free(client_.ssl);

  close(client_.sockfd);

  client_.ssl = nullptr; //so that if we try to close multiple times, free() won't crash on it, inside of wolfSSL_free()
  active_connections.erase(client_idx);
  client_.send_data = {}; //free up all the data we might have wanted to send

  freed_indexes.insert(client_idx);
}

void server<server_type::TLS>::write_connection(int client_idx, std::vector<char> &&buff, ulong custom_info) {
  auto *client = &clients[client_idx];
  client->custom_info = custom_info;
  client->send_data.push(write_data(std::move(buff)));
  const auto data_ref = client->send_data.front();
  
  if(client->send_data.size() == 1) //only do wolfSSL_write() if this is the only thing to write
    wolfSSL_write(client->ssl, &data_ref.buff[0], data_ref.buff.size()); //writes the data using wolfSSL
}

server<server_type::TLS>::server(
  int listen_port,
  std::string fullchain_location,
  std::string pkey_location,
  accept_callback<server_type::TLS> a_cb,
  read_callback<server_type::TLS> r_cb,
  write_callback<server_type::TLS> w_cb,
  void *custom_obj
){
  this->accept_cb = a_cb;
  this->read_cb = r_cb;
  this->write_cb = w_cb;
  this->custom_obj = custom_obj;

  //initialise wolfSSL
  wolfSSL_Init();

  //create the wolfSSL context
  if((wolfssl_ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method())) == NULL)
    fatal_error("Failed to create the WOLFSSL_CTX");

  //load the server certificate
  if(wolfSSL_CTX_use_certificate_chain_file(wolfssl_ctx, fullchain_location.c_str()) != SSL_SUCCESS)
    fatal_error("Failed to load the certificate files");

  //load the server's private key
  if(wolfSSL_CTX_use_PrivateKey_file(wolfssl_ctx, pkey_location.c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS)
    fatal_error("Failed to load the private key file");
  
  //set the wolfSSL callbacks
  wolfSSL_CTX_SetIORecv(wolfssl_ctx, tls_recv);
  wolfSSL_CTX_SetIOSend(wolfssl_ctx, tls_send);

  std::cout << "TLS will be used\n";
  
  std::memset(&ring, 0, sizeof(io_uring));
  io_uring_queue_init(QUEUE_DEPTH, &ring, 0); //no flags, setup the queue
  this->listener_fd = setup_listener(listen_port); //setup the listener socket
}

void server<server_type::TLS>::tls_accept(int client_idx){
  auto *client = &clients[client_idx];

  WOLFSSL *ssl = wolfSSL_new(wolfssl_ctx);
  wolfSSL_set_fd(ssl, client_idx); //not actually the fd but it's useful to us

  wolfSSL_SetIOReadCtx(ssl, this);
  wolfSSL_SetIOWriteCtx(ssl, this);

  client->ssl = ssl; //sets the ssl connection

  wolfSSL_accept(ssl); //initialise the wolfSSL accept procedure
}

void server<server_type::TLS>::server_loop(){
  running_server = true;

  io_uring_cqe *cqe;
  sockaddr_storage client_address;
  socklen_t client_address_length = sizeof(client_address);

  add_accept_req(listener_fd, &client_address, &client_address_length);

  while(true){
    char ret = io_uring_wait_cqe(&ring, &cqe);
    if(ret < 0)
      fatal_error("io_uring_wait_cqe");
    request *req = (request*)cqe->user_data;

      // std::cout << "Active connections: ";
      // for(const auto &connection : active_connections){
      //   std::cout << connection << " ";
      // }
      // std::cout << "\n";

      // size_t buffered{};
      // for(auto &client : clients){
      //   std::queue<write_data> temp_queue = client.send_data;
      //   while(!temp_queue.empty()){
      //     buffered += temp_queue.front().buff.size();
      //     temp_queue.pop();
      //   }
      //   buffered += client.recv_data.size();
      // }
      // std::cout << "\tcurrently buffered: " << buffered << std::endl;
      // std::cout << "\tmallocd: " << global_malloced << "\n";

    if(req->event != event_type::ACCEPT && (cqe->res <= 0 || clients[req->client_idx].id != req->ID)){
      if(req->event == event_type::ACCEPT_WRITE || req->event == event_type::WRITE)
        req->buffer = nullptr; //done with the request buffer
      if(cqe->res < 0 && clients[req->client_idx].id == req->ID){
        close_connection(req->client_idx); //making sure to remove any data relating to it as well
      }
    }else{
      switch(req->event){
        case event_type::ACCEPT: {
          auto client_idx = setup_client(cqe->res);
          add_accept_req(listener_fd, &client_address, &client_address_length);
          tls_accept(client_idx);
          req->buffer = nullptr; //done with the request buffer
          break;
        }
        case event_type::ACCEPT_READ: {
          auto &client = clients[req->client_idx];
          const auto &ssl = client.ssl;
          if(client.recv_data.size() == 0) { //if there is no data in the buffer, add it
            client.recv_data = std::vector<char>(req->buffer, req->buffer + cqe->res);
          }else{ //otherwise copy the new data to the end of the old data
            auto *vec = &client.recv_data;
            vec->insert(vec->end(), req->buffer, req->buffer + cqe->res);
          }
          if(wolfSSL_accept(ssl) == 1){ //that means the connection was successfully established
            if(accept_cb != nullptr) accept_cb(req->client_idx, this, custom_obj);
            active_connections.insert(req->client_idx);

            auto &data = client.recv_data; //the data vector
            const auto recvd_amount = data.size();
            std::vector<char> buffer(READ_SIZE);
            auto amount_read = wolfSSL_read(ssl, &buffer[0], READ_SIZE);

            //above will either add in a read request, or get whatever is left in the local buffer (as we might have got the HTTP request with the handshake)

            client.recv_data = std::vector<char>{};
            if(amount_read > -1)
              if(read_cb != nullptr) read_cb(req->client_idx, &buffer[0], amount_read, client.custom_info, this, custom_obj);
          }
          break;
        }
        case event_type::ACCEPT_WRITE: { //used only for when wolfSSL needs to write data during the TLS handshake
          auto &client = clients[req->client_idx];
          client.accept_last_written = cqe->res; //this is the amount that was last written, used in the tls_write callback
          wolfSSL_accept(client.ssl); //call accept again
          req->buffer = nullptr; //done with the request buffer
          break;
        }
        case event_type::WRITE: { //used for generally writing over TLS
          auto &client = clients[req->client_idx];
          if(client.send_data.size() > 0){ //ensure this connection is still active
            auto &data_ref = client.send_data.front();
            data_ref.last_written = cqe->res;

            int written = wolfSSL_write(client.ssl, &(data_ref.buff[0]), data_ref.buff.size());
            if(written > -1){ //if it's not negative, it's all been written, so this write call is done
              client.send_data.pop();
              if(write_cb != nullptr) write_cb(req->client_idx, client.custom_info, this, custom_obj);
              if(client.send_data.size()){ //if the write queue isn't empty, then write that as well
                auto &data_ref = client.send_data.front();
                wolfSSL_write(client.ssl, &(data_ref.buff[0]), data_ref.buff.size());
              }
            }
          }
          req->buffer = nullptr; //done with the request buffer
          break;
        }
        case event_type::READ: { //used for reading over TLS
          auto &client = clients[req->client_idx];
          int to_read_amount = cqe->res; //the default read size
          if(client.recv_data.size()) { //will correctly deal with needing to call wolfSSL_read multiple times
            auto &vec_member = client.recv_data;
            vec_member.insert(vec_member.end(), req->buffer, req->buffer + cqe->res);
            to_read_amount = vec_member.size(); //the read amount has got to be bigger, since the pending data could be more than READ_SIZE
          }else{
            client.recv_data = std::vector<char>(req->buffer, req->buffer + cqe->res);
          }

          std::vector<char> buffer(to_read_amount);
          int total_read = 0;

          while(client.recv_data.size()){
            int this_time = wolfSSL_read(client.ssl, &buffer[total_read], to_read_amount - total_read);
            if(this_time <= 0) break;
            total_read += this_time;
          }

          if(total_read == 0) add_read_req(req->client_idx, event_type::READ); //total_read of 0 implies that data must be read into the recv_data buffer
          
          if(total_read > 0){
            if(read_cb != nullptr) read_cb(req->client_idx, &buffer[0], total_read, client.custom_info, this, custom_obj);
            if(!client.recv_data.size())
              client.recv_data = {};
          }
          break;
        }
      }
    }

    //free any malloc'd data
    // CUSTOM_FREE(req->buffer);
    // CUSTOM_FREE(req);
    free(req->buffer);
    free(req);

    io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
  }
}