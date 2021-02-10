#include "../header/server.h"
#include "../header/utility.h"

template<server_type T>
void server_base<T>::start(){ //function to run the server
  std::cout << "Running server\n";
  if(!running_server) static_cast<server<T>*>(this)->server_loop();
}

template<server_type T>
int server_base<T>::setup_client(int client_socket){ //returns index into clients array
  auto index = 0;

  if(freed_indexes.size()){ //if there's a free index, give that
    index = *freed_indexes.begin(); //get first element in set
    freed_indexes.erase(index); //erase first element in set

    auto &freed_client = clients[index];

    const auto new_id = (freed_client.id + 1) % 100; //ID loops every 100
    freed_client = client<T>();
    freed_client.id = new_id;
  }else{
    clients.push_back(client<T>()); //otherwise give a new one
    index = clients.size()-1;
  }
  
  clients[index].sockfd = client_socket;

  return index;
}

template<server_type T>
void server_base<T>::read_connection(int client_idx, ulong custom_info) {
  clients[client_idx].custom_info = custom_info;
  add_read_req(client_idx, event_type::READ);
}

template<server_type T>
int server_base<T>::setup_listener(int port) {
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

template<server_type T>
int server_base<T>::add_accept_req(int listener_fd, sockaddr_storage *client_address, socklen_t *client_address_length){
  io_uring_sqe *sqe = io_uring_get_sqe(&ring); //get a valid SQE (correct index and all)
  io_uring_prep_accept(sqe, listener_fd, (sockaddr*)client_address, client_address_length, 0); //no flags set, prepares an SQE

  request *req = (request*)std::malloc(sizeof(request));
  // request *req = (request*)CUSTOM_MALLOC(sizeof(request));
  req->event = event_type::ACCEPT;

  io_uring_sqe_set_data(sqe, req); //sets the SQE data
  io_uring_submit(&ring); //submits the event

  return 0; //maybe return is required for something else later
}

template<server_type T>
int server_base<T>::add_read_req(int client_idx, event_type event){
  io_uring_sqe *sqe = io_uring_get_sqe(&ring); //get a valid SQE (correct index and all)
  request *req = (request*)std::malloc(sizeof(request)); //enough space for the request struct
  req->buffer = (char*)std::malloc(READ_SIZE); //malloc enough space for the data to be read
  // request *req = (request*)CUSTOM_MALLOC(sizeof(request)); //enough space for the request struct
  // req->buffer = (char*)CUSTOM_MALLOC(READ_SIZE); //malloc enough space for the data to be read
  req->total_length = READ_SIZE;;
  req->event = event;
  req->client_idx = client_idx;
  req->ID = clients[client_idx].id;
  std::memset(req->buffer, 0, READ_SIZE);
  
  io_uring_prep_read(sqe, clients[client_idx].sockfd, req->buffer, READ_SIZE, 0); //don't read at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}

template<server_type T>
int server_base<T>::add_write_req(int client_idx, event_type event, char *buffer, unsigned int length) {
  // request *req = (request*)CUSTOM_MALLOC(sizeof(request));
  request *req = (request*)std::malloc(sizeof(request));
  std::memset(req, 0, sizeof(request));
  req->client_idx = client_idx;
  req->total_length = length;
  req->buffer = buffer;
  req->event = event;
  req->ID = clients[client_idx].id;
  
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_write(sqe, clients[client_idx].sockfd, buffer, length, 0); //do not write at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}