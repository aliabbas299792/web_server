#include "../header/server.h"

server::server(int listen_port, accept_callback a_cb, read_callback r_cb, write_callback w_cb, void *custom_obj) : a_cb(a_cb), r_cb(r_cb), w_cb(w_cb), custom_obj(custom_obj) {
  //above just sets the callbacks
  std::memset(&ring, 0, sizeof(io_uring));
  io_uring_queue_init(QUEUE_DEPTH, &ring, 0); //no flags, setup the queue
  this->listener_fd = setup_listener(listen_port); //setup the listener socket
}

void server::start(){ //function to run the server
  std::cout << "Running server\n";
  if(!running_server) this->serverLoop();
}

void server::close_socket(int client_socket){ //closes the socket and makes sure to delete it from all of the data structures
  if(is_tls){
    auto ssl = socket_to_ssl[client_socket];
    wolfSSL_shutdown(ssl);
    wolfSSL_free(ssl);
    active_connections.erase(client_socket);
    socket_to_ssl.erase(client_socket);
    send_data.erase(client_socket);
    recv_data.erase(client_socket);
  }
  close(client_socket); //finally, close the socket
}

void server::write_socket(int client_socket, std::vector<char> &&buff){
  if(is_tls){
    send_data[client_socket].push(write_data(std::move(buff))); //adds this to the write queue for this socket
    if(send_data[client_socket].size() == 1) //if only thing to write is what we just pushed, then call wolfSSL_write
      wolfSSL_write(socket_to_ssl[client_socket], &buff[0], buff.size()); //writes the file using wolfSSL
  }else{
    add_write_req(client_socket, &buff[0], buff.size()); //adds a plain HTTP write request
  }
}

void server::read_socket(int client_socket){
  add_read_req(client_socket, false);
}

void server::tls_accept(int client_socket){
  WOLFSSL *ssl = wolfSSL_new(wolfssl_ctx);
  wolfSSL_set_fd(ssl, client_socket);

  wolfSSL_SetIOReadCtx(ssl, this);
  wolfSSL_SetIOWriteCtx(ssl, this);

  socket_to_ssl[client_socket] = ssl; //sets the ssl connection

  wolfSSL_accept(ssl); //initialise the wolfSSL accept procedure
}

void server::setup_tls(std::string fullchain_location, std::string pkey_location){
  is_tls = true; //is now using TLS

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
}

int server::setup_listener(int port) {
  int listener_fd;
  int yes = 1;
  addrinfo hints, *server_info, *traverser;

  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; //IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; //tcp
  hints.ai_flags = AI_PASSIVE; //use local IP

  if(getaddrinfo(NULL, std::to_string(port).c_str(), &hints, &server_info) != 0)
    fatal_error("getaddrinfo");

  for(traverser = server_info; traverser != NULL; traverser = traverser->ai_next){
    if((listener_fd = socket(traverser->ai_family, traverser->ai_socktype, traverser->ai_protocol)) == -1) //ai_protocol may be usefulin the future I believe, only UDP/TCP right now, may
      fatal_error("socket construction");

    if(setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) //2nd param (SOL_SOCKET) is saying to do it at the socket protocol level, not TCP or anything else, just for the socket
      fatal_error("setsockopt SO_REUSEADDR");
      
    if(setsockopt(listener_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1)
      fatal_error("setsockopt SO_REUSEPORT");

    if(bind(listener_fd, traverser->ai_addr, traverser->ai_addrlen) == -1){ //try to bind the socket using the address data supplied, has internet address, address family and port in the data
      perror("bind");
      continue; //not fatal, we can continue
    }

    break; //we got here, so we've got a working socket - so break
  }

  freeaddrinfo(server_info); //free the server_info linked list

  if(traverser == NULL) //means we didn't break, so never got a socket made successfully
    fatal_error("no socket made");

  if(listen(listener_fd, BACKLOG) == -1)
    fatal_error("listen");

  return listener_fd;
}

int server::add_accept_req(int listener_fd, sockaddr_storage *client_address, socklen_t *client_address_length){
  io_uring_sqe *sqe = io_uring_get_sqe(&ring); //get a valid SQE (correct index and all)
  io_uring_prep_accept(sqe, listener_fd, (sockaddr*)client_address, client_address_length, 0); //no flags set, prepares an SQE

  request *req = (request*)std::malloc(sizeof(request));
  req->event = event_type::ACCEPT;

  io_uring_sqe_set_data(sqe, req); //sets the SQE data
  io_uring_submit(&ring); //submits the event

  return 0; //maybe return is required for something else later
}

int server::add_read_req(int client_socket, bool accept){
  io_uring_sqe *sqe = io_uring_get_sqe(&ring); //get a valid SQE (correct index and all)
  request *req = (request*)std::malloc(sizeof(request)); //enough space for the request struct
  req->buffer = (char*)std::malloc(READ_SIZE); //malloc enough space for the data to be read
  req->total_length = READ_SIZE;

  auto ssl = socket_to_ssl[client_socket];
  
  req->client_socket = client_socket;
  std::memset(req->buffer, 0, READ_SIZE);

  if(accept){
    req->ssl = ssl;
    req->event = event_type::ACCEPT_READ_SSL;
  }else if(is_tls){
    req->ssl = ssl;
    req->event = event_type::READ_SSL;
  }else{
    req->event = event_type::READ;
  }
  
  io_uring_prep_read(sqe, client_socket, req->buffer, READ_SIZE, 0); //don't read at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}

int server::add_write_req(int client_socket, char *buffer, unsigned int length, bool accept) {
  request *req = (request*)std::malloc(sizeof(request));
  std::memset(req, 0, sizeof(request));
  req->client_socket = client_socket;
  req->total_length = length;
  req->buffer = buffer;

  if(accept){
    req->event = event_type::ACCEPT_WRITE_SSL;
  }else if(is_tls){
    req->event = event_type::WRITE_SSL;
  }else{
    req->event = event_type::WRITE;
  }
  
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_write(sqe, req->client_socket, buffer, length, 0); //do not write at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}

int server::add_write_req_continued(request *req, int written) { //for long plain HTTP write requests, this writes at the correct offset
  req->written += written;
  
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_write(sqe, req->client_socket, &req->buffer[req->written], req->total_length - req->written, 0); //do not write at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}

void server::serverLoop(){
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
    
    switch(req->event){
      case event_type::ACCEPT: {
        add_accept_req(listener_fd, &client_address, &client_address_length);
        if(is_tls) {
          tls_accept(cqe->res);
        }else{
          if(a_cb != nullptr) a_cb(cqe->res, this, custom_obj);
          add_read_req(cqe->res); //also need to read whatever request it sends immediately
        }
        req->buffer = nullptr; //done with the request buffer
        break;
      }
      case event_type::READ: {
        if(cqe->res > 0)
          if(r_cb != nullptr) r_cb(req->client_socket, req->buffer, cqe->res, this, custom_obj);
        break;
      }
      case event_type::WRITE: {
        if(cqe->res + req->written < req->total_length && cqe->res > 0){
          int rc = add_write_req_continued(req, cqe->res);
          req->buffer = nullptr; //done with the request buffer
          if(rc == 0) break;
        }
        if(w_cb != nullptr) w_cb(req->client_socket, this, custom_obj);
        req->buffer = nullptr; //done with the request buffer
        break;
      }
      case event_type::ACCEPT_READ_SSL: {
        if(cqe->res > 0 && socket_to_ssl.count(req->client_socket)) { //if an error occurred, don't try to negotiate the connection
          auto ssl = socket_to_ssl[req->client_socket];
          if(!recv_data.count(req->client_socket)){ //if there is no data in the map, add it
            recv_data[req->client_socket] = std::vector<char>(req->buffer, req->buffer + cqe->res);
          }else{ //otherwise copy the new data to the end of the old data
            auto *buffer = &recv_data[req->client_socket];
            buffer->insert(buffer->end(), req->buffer, req->buffer + cqe->res);
          }
          if(wolfSSL_accept(ssl) == 1){ //that means the connection was successfully established
            if(a_cb != nullptr) a_cb(req->client_socket, this, custom_obj);
            active_connections.insert(req->client_socket);

            std::vector<char> buffer(READ_SIZE);
            auto amount_read = wolfSSL_read(socket_to_ssl[req->client_socket], &buffer[0], READ_SIZE);
            //above will either add in a read request, or get whatever is left in the local buffer (as we might have got the HTTP request with the handshake)

            recv_data.erase(req->client_socket);
            if(amount_read > -1)
              if(r_cb != nullptr) r_cb(req->client_socket, &buffer[0], amount_read, this, custom_obj);
          }
        }else{
          close_socket(req->client_socket); //making sure to remove any data relating to it as well
        }
        break;
      }
      case event_type::ACCEPT_WRITE_SSL: { //used only for when wolfSSL needs to write data during the TLS handshake
        if(cqe->res <= 0 || !socket_to_ssl.count(req->client_socket)) { //if an error occurred, don't try to negotiate the connection
          close_socket(req->client_socket); //making sure to remove any data relating to it as well
        }else{
          accept_send_data[req->client_socket] = cqe->res; //this is the amount that was last written, used in the tls_write callback
          wolfSSL_accept(socket_to_ssl[req->client_socket]); //call accept again
        }
        req->buffer = nullptr; //done with the request buffer
        break;
      }
      case event_type::WRITE_SSL: { //used for generally writing over TLS
        if(cqe->res > 0 && socket_to_ssl.count(req->client_socket) && send_data.count(req->client_socket) && send_data[req->client_socket].size() > 0){ //ensure this connection is still active
          auto *data_ref = &send_data[req->client_socket].front();
          data_ref->last_written = cqe->res;
          int written = wolfSSL_write(socket_to_ssl[req->client_socket], &(data_ref->buff[0]), data_ref->buff.size());
          if(written > -1){ //if it's not negative, it's all been written, so this write call is done
            send_data[req->client_socket].pop();
            if(w_cb != nullptr) w_cb(req->client_socket, this, custom_obj);
            if(send_data[req->client_socket].size()){ //if the write queue isn't empty, then write that as well
              data_ref = &send_data[req->client_socket].front();
              wolfSSL_write(socket_to_ssl[req->client_socket], &(data_ref->buff[0]), data_ref->buff.size());
            }
          }
        }else{
          close_socket(req->client_socket); //otherwise make sure that the socket is closed properly
        }
        req->buffer = nullptr; //done with the request buffer
        break;
      }
      case event_type::READ_SSL: { //used for reading over TLS
        if(cqe->res > 0 && socket_to_ssl.count(req->client_socket)){
          int to_read_amount = cqe->res; //the default read size
          if(recv_data.count(req->client_socket)){ //will correctly deal with needing to call wolfSSL_read multiple times
            auto *vec_member = &recv_data[req->client_socket];
            vec_member->insert(vec_member->end(), req->buffer, req->buffer + cqe->res);
            to_read_amount = vec_member->size(); //the read amount has got to be bigger, since the pending data could be more than READ_SIZE
          }else{
            recv_data[req->client_socket] = std::vector<char>(req->buffer, req->buffer + cqe->res);
          }

          std::vector<char> buffer(to_read_amount);
          int total_read = 0;

          while(recv_data[req->client_socket].size()){
            int this_time = wolfSSL_read(socket_to_ssl[req->client_socket], &buffer[total_read], to_read_amount - total_read);
            if(this_time <= 0) break;
            total_read += this_time;
          }

          if(total_read == 0) add_read_req(req->client_socket); //total_read of 0 implies that data must be read into the recv_data buffer
          
          if(total_read > 0){
           if(r_cb != nullptr) r_cb(req->client_socket, &buffer[0], total_read, this, custom_obj);
            if(!recv_data[req->client_socket].size())
              recv_data.erase(req->client_socket);
          }
        }else{
          close_socket(req->client_socket);
        }
        break;
      }
    }

    //free any malloc'd data
    free(req->buffer);
    free(req);

    io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
  }
}