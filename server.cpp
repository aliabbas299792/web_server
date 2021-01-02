#include "server.h"

void fatal_error(std::string error_message){
  perror(std::string("Fatal Error: " + error_message).c_str());
  exit(1);
}

server::server(accept_callback a_cb, read_callback r_cb, write_callback w_cb, void *custom_obj) : a_cb(a_cb), r_cb(r_cb), w_cb(w_cb), custom_obj(custom_obj) {
  //above just sets the callbacks

  io_uring_queue_init(QUEUE_DEPTH, &ring, 0); //no flags, setup the queue

  this->listener_fd = setup_listener(PORT);
  
  this->serverLoop();
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

int server::add_read_req(int client_fd){
  io_uring_sqe *sqe = io_uring_get_sqe(&ring); //get a valid SQE (correct index and all)
  request *req = (request*)std::malloc(sizeof(request)); //enough space for the request struct
  req->buffer = (char*)std::malloc(READ_SIZE); //malloc enough space for the data to be read
  req->total_length = READ_SIZE;
  
  req->event = event_type::READ;
  req->client_socket = client_fd;
  std::memset(req->buffer, 0, READ_SIZE);
  
  io_uring_prep_read(sqe, client_fd, req->buffer, READ_SIZE, 0); //don't read at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}

int server::add_write_req(int client_fd, char *buffer, unsigned int length) {
  request *req = (request*)std::malloc(sizeof(request));
  std::memset(req, 0, sizeof(request));
  req->client_socket = client_fd;
  req->total_length = length;
  req->event = event_type::WRITE;
  req->buffer = buffer;
  
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
        if(a_cb != nullptr) a_cb(cqe->res, this, custom_obj);
        add_accept_req(listener_fd, &client_address, &client_address_length); //add another accept request
        add_read_req(cqe->res); //also need to read whatever request it sends immediately
        free(req); //cleanup from the malloc in add_accept_req
        break;
      }
      case event_type::READ: {
        if(r_cb != nullptr) r_cb(req->client_socket, req->buffer, req->total_length, this, custom_obj);
        //below is cleaning up from the malloc stuff
        free(req->buffer);
        free(req);
        break;
      }
      case event_type::WRITE: {
        if(cqe->res + req->written < req->total_length && cqe->res > 0){
          int rc = add_write_req_continued(req, cqe->res);
          if(rc == 0) break;
        }
        if(w_cb != nullptr) w_cb(req->client_socket, this, custom_obj);
        //below is cleaning up from the malloc stuff
        free(req->buffer);
        free(req);
        break;
      }
    }

    io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
  }
}