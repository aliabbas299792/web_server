#include "../header/server.h"

int tls_send(WOLFSSL* ssl, char* buff, int sz, void* ctx){ //send callback, sends a special accept write request to io_uring, make it not always do that
  int sockfd = ((rw_cb_context*)ctx)->sockfd;
  auto *tcp_server = ((rw_cb_context*)ctx)->tcp_server;

  if(tcp_server->active_connections.count(sockfd)) std::cout << "want to write: " << sz << "\n";
  tcp_server->add_write_req(sockfd, buff, sz, true);

  return sz;
}

int tls_recv_helper(std::unordered_map<int, std::pair<char*, int>> *recvd_data, server *tcp_server, char *buff, int sz, int sockfd, bool accept){
  const auto data = (*recvd_data)[sockfd];
  const auto all_received = data.first; //we don't free this buffer in this function, since it's taken care of in the io_uring loop
  const auto recvdAmount = data.second;
  if(recvdAmount > sz){
    //if the data needed in this call is less than what we have available,
    //then copy that onto the provided buffer, and return the amount read
    std::memcpy(buff, all_received, sz);
    (*recvd_data)[sockfd] = { (char*)std::malloc(recvdAmount - sz), recvdAmount - sz };
    std::memset((*recvd_data)[sockfd].first, 0, recvdAmount - sz);
    std::memcpy((*recvd_data)[sockfd].first, &all_received[sz], recvdAmount - sz);
    return sz;
  }else if(recvdAmount < sz){ //in the off chance that there isn't enough data available for the full request
    if(accept)
      tcp_server->add_read_req(sockfd, true);
    else
      tcp_server->add_read_req(sockfd, false, true);
    return WOLFSSL_CBIO_ERR_WANT_READ; //if there was no data to be read currently, send a request for more data, and respond with this error
  }else{
    std::memcpy(buff, all_received, sz); //since this is exactly how much we need, copy the data into the buffer
    recvd_data->erase(sockfd); //and erasing the item from the map, since we've used all the data it provided
    return recvdAmount; //sz == recvdAmount in this case
  }
}

int tls_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx){ //receive callback
  int sockfd = ((rw_cb_context*)ctx)->sockfd;
  auto *tcp_server = ((rw_cb_context*)ctx)->tcp_server;
  auto *recvd_data = &tcp_server->recvd_data;

  if(tcp_server->active_connections.count(sockfd)){
    return tls_recv_helper(recvd_data, tcp_server, buff, sz, sockfd, false);
  }else{
    if(recvd_data->count(sockfd)){ //if an entry exists in the map, use the data in it, otherwise make a request for it
      return tls_recv_helper(recvd_data, tcp_server, buff, sz, sockfd, true);
    }else{
      tcp_server->add_read_req(sockfd, true);
      return WOLFSSL_CBIO_ERR_WANT_READ; //if there was no data to be read currently, send a request for more data, and respond with this error
    } 
  }
}

void server::start(){ //function to run the server
  if(!running_server) this->serverLoop();
}

void server::write(int client_fd, char *buff, unsigned int length){
  if(is_tls){
    int rc = wolfSSL_write(socket_to_ssl[client_fd], buff, length); //just add in a way to properly write everything, and use WOLFSSL_CBIO_ERR_WANT_WRITE
    std::cout << "written: " << rc << "\n";
    std::cout << "wanted to write properly: " << length << "\n";
  }else{
    add_write_req(client_fd, buff, length);
  }
}

void server::close(int client_fd){
  wolfSSL_shutdown(socket_to_ssl[client_fd]);
  active_connections.erase(client_fd);
  socket_to_ssl.erase(client_fd);
  recvd_data.erase(client_fd);
  close(client_fd);
}

void server::setup_tls(std::string cert_location, std::string pkey_location){
  is_tls = true; //is now using TLS

  //initialise wolfSSL
  wolfSSL_Init();

  //create the wolfSSL context
  if ((wolfssl_ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method())) == NULL)
    fatal_error("Failed to create the WOLFSSL_CTX");

  //load the server certificate
  if (wolfSSL_CTX_use_certificate_file(wolfssl_ctx, cert_location.c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS)
    fatal_error("Failed to load the certificate files");

  //load the server's private key
  if (wolfSSL_CTX_use_PrivateKey_file(wolfssl_ctx, pkey_location.c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS)
    fatal_error("Failed to load the private key file");
  
  //set the wolfSSL callbacks
  wolfSSL_CTX_SetIORecv(wolfssl_ctx, tls_recv);
  wolfSSL_CTX_SetIOSend(wolfssl_ctx, tls_send);
}

void server::tls_accept(int client_fd){
  WOLFSSL *ssl = wolfSSL_new(wolfssl_ctx);
  wolfSSL_set_fd(ssl, client_fd);

  //set the read/write context data, from this scope,
  //since once execution leaves this scope the references are invalid and we'll have to set the context data again
  rw_cb_context ctx_data(this, client_fd);
  wolfSSL_SetIOReadCtx(ssl, &ctx_data);
  wolfSSL_SetIOWriteCtx(ssl, &ctx_data);

  std::cout << "new made\n";
  socket_to_ssl[client_fd] = ssl;

  wolfSSL_accept(ssl); //initialise the wolfSSL accept procedure
}

server::server(int listen_port, accept_callback a_cb, read_callback r_cb, write_callback w_cb, void *custom_obj) : a_cb(a_cb), r_cb(r_cb), w_cb(w_cb), custom_obj(custom_obj) {
  //above just sets the callbacks

  io_uring_queue_init(QUEUE_DEPTH, &ring, 0); //no flags, setup the queue

  this->listener_fd = setup_listener(listen_port);
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

int server::add_read_req(int client_fd, bool accept, bool read_ssl){
  io_uring_sqe *sqe = io_uring_get_sqe(&ring); //get a valid SQE (correct index and all)
  request *req = (request*)std::malloc(sizeof(request)); //enough space for the request struct
  req->buffer = (char*)std::malloc(READ_SIZE); //malloc enough space for the data to be read
  req->total_length = READ_SIZE;

  auto ssl = socket_to_ssl[client_fd];
  
  req->client_socket = client_fd;
  std::memset(req->buffer, 0, READ_SIZE);

  if(accept){
    req->ssl = ssl;
    req->event = event_type::READ_ACCEPT;
  }else if(read_ssl){
    req->ssl = ssl;
    req->event = event_type::READ_SSL;
  }else{
    req->event = event_type::READ;
  }
  
  io_uring_prep_read(sqe, client_fd, req->buffer, READ_SIZE, 0); //don't read at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}

int server::add_write_req(int client_fd, char *buffer, unsigned int length, bool accept) {
  request *req = (request*)std::malloc(sizeof(request));
  std::memset(req, 0, sizeof(request));
  req->client_socket = client_fd;
  req->total_length = length;

  if(accept){
    req->event = event_type::WRITE_ACCEPT;
  }else{
    req->event = event_type::WRITE;
    req->buffer = buffer;
  }

  if(is_tls){
    std::cout << "copying the big buffer of length " << length << "\n";
    req->buffer = (char*)std::malloc(length);
    std::memcpy(req->buffer, buffer, length);
  }
  
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_write(sqe, req->client_socket, buffer, length, 0); //do not write at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}

int server::add_write_req_continued(request *req, int written) {
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
    
    switch(req->event){
      case event_type::ACCEPT: {
        if(is_tls) {
          tls_accept(cqe->res);
        }else{
          if(a_cb != nullptr) a_cb(cqe->res, this, custom_obj);
          add_read_req(cqe->res); //also need to read whatever request it sends immediately
        }
        add_accept_req(listener_fd, &client_address, &client_address_length); //add another accept request
        free(req); //cleanup from the malloc in add_accept_req
        break;
      }
      case event_type::READ: {
        if(r_cb != nullptr) r_cb(req->client_socket, req->buffer, cqe->res, this, custom_obj);
        //below is cleaning up from the malloc stuff

        add_read_req(req->client_socket);

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
      case event_type::READ_ACCEPT: {
        if(!recvd_data.count(req->client_socket)){ //stores the data in the map
          recvd_data[req->client_socket] = { req->buffer, cqe->res };
        }else{ //copies the new data to the end of the offset
          const auto old_data = recvd_data[req->client_socket];
          char *new_buff = (char*)std::malloc(cqe->res + old_data.second);
          std::memcpy(new_buff, old_data.first, old_data.second);
          std::memcpy(&new_buff[old_data.second], req->buffer, cqe->res);
        }
        
        //set the read/write context data, from this scope,
        //since once execution leaves this scope the references are invalid and we'll have to set the context data again
        rw_cb_context ctx_data(this, req->client_socket);
        wolfSSL_SetIOReadCtx(req->ssl, &ctx_data);
        wolfSSL_SetIOWriteCtx(req->ssl, &ctx_data);

        if(wolfSSL_accept(req->ssl) == 1){ //that means the connection was successfully established
          if(a_cb != nullptr) a_cb(cqe->res, this, custom_obj);
          active_connections.insert(req->client_socket);
          add_read_req(req->client_socket, false, true);
        }
        free(req->buffer); //free the buffer used
        free(req); //free the memory allocated for the request, but not the memory for the buffer itself, that's handled in the receive callback
        break;
      }
      case event_type::WRITE_ACCEPT: {
        //you don't free the request buffer here, since wolfSSL takes care of that
        free(req); //free the memory allocated for the request, but not the memory for the buffer itself, that's handled in the receive callback
        break;
      }
      case event_type::READ_SSL: {
        recvd_data[req->client_socket] = { req->buffer, cqe->res };
        char *buffer = (char*)std::malloc(READ_SIZE);
        
        //set the read/write context data, from this scope,
        //since once execution leaves this scope the references are invalid and we'll have to set the context data again
        rw_cb_context ctx_data(this, req->client_socket);
        wolfSSL_SetIOReadCtx(req->ssl, &ctx_data);
        wolfSSL_SetIOWriteCtx(req->ssl, &ctx_data);

        int amount_read = wolfSSL_read(socket_to_ssl[req->client_socket], buffer, READ_SIZE);
        std::cout << "amout read: " << wolfSSL_get_error(req->ssl, amount_read) << "\n";
        if(r_cb != nullptr) r_cb(req->client_socket, buffer, amount_read, this, custom_obj);

        add_read_req(req->client_socket, false, true); //adds another read request

        free(buffer);
        free(req->buffer);
        free(req);
      }
    }

    io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
  }
}