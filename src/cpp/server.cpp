#include "../header/server.h"

server::server(int listen_port, accept_callback a_cb, read_callback r_cb, write_callback w_cb, void *custom_obj) : a_cb(a_cb), r_cb(r_cb), w_cb(w_cb), custom_obj(custom_obj) {
  //above just sets the callbacks
  io_uring_queue_init(QUEUE_DEPTH, &ring, 0); //no flags, setup the queue
  this->listener_fd = setup_listener(listen_port); //setup the listener socket
}

void server::start(){ //function to run the server
  if(!running_server) this->serverLoop();
}

void server::close_socket(int client_socket){ //closes the socket and makes sure to delete it from all of the data structures
  if(is_tls){
    wolfSSL_shutdown(socket_to_ssl[client_socket]);
    active_connections.erase(client_socket);
    socket_to_context.erase(client_socket);
    socket_to_ssl.erase(client_socket);
    
    //make sure to free any buffers and then erase the item
    auto *queue = &send_data[client_socket];
    while(queue->size()){
      free(queue->front().buff);
      queue->pop();
    }
    send_data.erase(client_socket);
    
    //make sure to free any buffer and then erase the item
    free(accept_recv_data[client_socket].first);
    accept_recv_data.erase(client_socket);
  }
  close(client_socket); //finally, close the socket
}

void server::write_socket(int client_socket, char *buff, unsigned int length){
  if(is_tls){
    send_data[client_socket].push(write_data(buff, length)); //adds this to the write queue for this socket
    if(send_data[client_socket].size() == 1) //if only thing to write is what we just pushed, then call wolfSSL_write
      wolfSSL_write(socket_to_ssl[client_socket], buff, length); //writes the file using wolfSSL
  }else{
    add_write_req(client_socket, buff, length); //adds a plain HTTP write request
  }
}

void server::tls_accept(int client_socket){
  WOLFSSL *ssl = wolfSSL_new(wolfssl_ctx);
  wolfSSL_set_fd(ssl, client_socket);

  //set the read/write context data, from this scope,
  //since once execution leaves this scope the references are invalid and we'll have to set the context data again
  rw_cb_context ctx_data(this, client_socket);
  socket_to_context[client_socket] = ctx_data; //sets the context data

  const auto ctx_ref = &socket_to_context[client_socket];
  wolfSSL_SetIOReadCtx(ssl, ctx_ref);
  wolfSSL_SetIOWriteCtx(ssl, ctx_ref);

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
  std::cout << "submit: " << io_uring_submit(&ring) << "\n"; //submits the event

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
    request *req = (request*)cqe->user_data;

    if(ret < 0)
      fatal_error("io_uring_wait_cqe");
    
    // std::cout << "an event occurred...\n";
    
    switch(req->event){
      case event_type::ACCEPT: {
        if(is_tls) {
          tls_accept(cqe->res);
        }else{
          if(a_cb != nullptr) a_cb(cqe->res, this, custom_obj);
          add_read_req(cqe->res); //also need to read whatever request it sends immediately
        }
        add_accept_req(listener_fd, &client_address, &client_address_length);
        free(req); //cleanup from the malloc in add_accept_req
        break;
      }
      case event_type::READ: {
        if(r_cb != nullptr) r_cb(req->client_socket, req->buffer, cqe->res, this, custom_obj);
        add_read_req(req->client_socket);
        //below is cleaning up from the malloc stuff
        free(req->buffer);
        free(req);
        break;
      }
      case event_type::WRITE: {
        if(req->ssl == nullptr){
          if(cqe->res + req->written < req->total_length && cqe->res > 0){
            int rc = add_write_req_continued(req, cqe->res);
            if(rc == 0) break;
          }
          if(w_cb != nullptr) w_cb(req->client_socket, this, custom_obj);
        }
        //below is cleaning up from the malloc stuff
        free(req->buffer);
        free(req);
        break;
      }
      case event_type::ACCEPT_READ_SSL: {
        if(cqe->res <= 0) { //if an error occurred, don't try to negotiate the connection
          close_socket(req->client_socket); //making sure to remove any data relating to it as well
          break;
        }
        if(!accept_recv_data.count(req->client_socket)){ //if there is no data in the map, add it
          accept_recv_data[req->client_socket] = { req->buffer, cqe->res };
        }else{ //otherwise copy the new data to the end of the old data
          const auto *old_data = &accept_recv_data[req->client_socket];
          char *new_buff = (char*)std::malloc(cqe->res + old_data->second);
          std::memcpy(new_buff, old_data->first, old_data->second);
          std::memcpy(&new_buff[old_data->second], req->buffer, cqe->res);
          free(old_data->first); //frees the old buffer
        }
        if(wolfSSL_accept(req->ssl) == 1){ //that means the connection was successfully established
          if(a_cb != nullptr) a_cb(req->client_socket, this, custom_obj);
          active_connections.insert(req->client_socket);

          char *buffer = (char*)std::malloc(READ_SIZE);
          auto amount_read = wolfSSL_read(socket_to_ssl[req->client_socket], buffer, READ_SIZE);
          //above will either add in a read request, or get whatever is left in the local buffer (as we might have got the HTTP request with the handshake)

          free(accept_recv_data[req->client_socket].first); //now that we're done with this data - free it
          accept_recv_data[req->client_socket].first = nullptr; //makes it possible to avoid double free or corruption error (since you can call free on nullptr)
          if(amount_read > -1)
            if(r_cb != nullptr) r_cb(req->client_socket, buffer, amount_read, this, custom_obj);
          free(buffer);
        }
        free(req->buffer); //free the buffer used
        free(req); //free the memory allocated for the request, but not the memory for the buffer itself, that's handled in the receive callback
        break;
      }
      case event_type::ACCEPT_WRITE_SSL: { //used only for when wolfSSL needs to write data during the TLS handshake
        if(cqe->res <= 0) { //if an error occurred, don't try to negotiate the connection
          close_socket(req->client_socket); //making sure to remove any data relating to it as well
          break;
        }
        accept_send_data[req->client_socket] = cqe->res; //this is the amount that was last written, used in the tls_write callback
        wolfSSL_accept(socket_to_ssl[req->client_socket]); //call accept again
        free(req); //free the memory allocated for the request, but not the memory for the buffer itself, that's handled in the receive callback
        break;
      }
      case event_type::WRITE_SSL: { //used for generally writing over TLS
        if(active_connections.count(req->client_socket)){ //ensure this connection is still active
          auto *data_ref = &send_data[req->client_socket].front();
          data_ref->last_written = cqe->res;
          int written = wolfSSL_write(socket_to_ssl[req->client_socket], data_ref->buff, data_ref->total_length);
          if(written > -1){ //if it's not negative, it's all been written, so this write call is done
            free(data_ref->buff);
            // data_ref->buff = nullptr; //makes it possible to avoid double free or corruption error (since you can call free on nullptr)
            send_data[req->client_socket].pop();
            if(w_cb != nullptr) w_cb(req->client_socket, this, custom_obj);
            if(send_data[req->client_socket].size()){ //if the write queue isn't empty, then write that as well
              data_ref = &send_data[req->client_socket].front();
              wolfSSL_write(socket_to_ssl[req->client_socket], data_ref->buff, data_ref->total_length);
            }
          }
        }else{
          close_socket(req->client_socket); //otherwise make sure that the socket is closed properly
        }
        free(req); //free the memory allocated for the request, but not the memory for the buffer itself, that's handled in the receive callback
        break;
      }
      case event_type::READ_SSL: { //used for reading over TLS
        if(accept_recv_data.count(req->client_socket)){
          free(accept_recv_data[req->client_socket].first);
        }
        accept_recv_data[req->client_socket] = { req->buffer, cqe->res };
        char *buffer = (char*)std::malloc(READ_SIZE);
        int amount_read = 0;
        if(cqe->res > 0) //if read 0 bytes, it means at end of file
          amount_read = wolfSSL_read(socket_to_ssl[req->client_socket], buffer, READ_SIZE);
        if(amount_read > 0) //a non-zero amount read implies that reading has finished
          if(r_cb != nullptr) r_cb(req->client_socket, buffer, amount_read, this, custom_obj);
        //free all of the allocated memory
        free(buffer);
        free(req->buffer);
        free(req);
        break;
      }
    }

    io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
  }
}