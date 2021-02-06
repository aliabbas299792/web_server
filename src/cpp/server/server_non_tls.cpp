#include "../../header/server.h"
#include "../../header/utility.h"

server<server_type::NON_TLS>::server(
  int listen_port,
  accept_callback<server_type::NON_TLS> a_cb,
  read_callback<server_type::NON_TLS> r_cb,
  write_callback<server_type::NON_TLS> w_cb,
  void *custom_obj
){
  this->accept_cb = a_cb;
  this->read_cb = r_cb;
  this->write_cb = w_cb;
  this->custom_obj = custom_obj;

  std::memset(&ring, 0, sizeof(io_uring));
  io_uring_queue_init(QUEUE_DEPTH, &ring, 0); //no flags, setup the queue
  this->listener_fd = setup_listener(listen_port); //setup the listener socket
}

void server<server_type::NON_TLS>::write_connection(int client_idx, std::vector<char> &&buff) {
  auto *client = &clients[client_idx];
  client->send_data.push(write_data(std::move(buff)));
  if(client->send_data.size() == 1){ //only adds a write request in the case that the queue was empty before this
    const auto data_ref = client->send_data.front();
    add_write_req(client_idx, event_type::WRITE, (char*)&data_ref.buff[0], data_ref.buff.size());
  }
}

void server<server_type::NON_TLS>::close_connection(int client_idx) {
  auto *client = &clients[client_idx];

  active_connections.erase(client_idx);

  close(client->sockfd);
  
  client->sockfd = 0;
  client->send_data = {};

  freed_indexes.push(client_idx);
}

int server<server_type::NON_TLS>::add_write_req_continued(request *req, int written) { //for long plain HTTP write requests, this writes at the correct offset
  auto &client = clients[req->client_idx];
  auto &to_write = client.send_data.front();

  req->written += written;
  
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_write(sqe, client.sockfd, &to_write.buff[req->written], req->total_length - req->written, 0); //do not write at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}

void server<server_type::NON_TLS>::server_loop(){
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
        auto client_idx = setup_client(cqe->res);
        active_connections.insert(client_idx); 
        //above basically says this connection is now active, checking if this connection replaced an existing but broken one happens elsewhere

        if(accept_cb != nullptr) accept_cb(client_idx, this, custom_obj);
        
        add_read_req(client_idx, event_type::READ); //also need to read whatever request it sends immediately
        req->buffer = nullptr; //done with the request buffer
        break;
      }
      case event_type::READ: {
        if(cqe->res > 0)
          if(read_cb != nullptr) read_cb(req->client_idx, req->buffer, cqe->res, this, custom_obj);
        else
          close_connection(req->client_idx);
        break;
      }
      case event_type::WRITE: {
        bool error = false;
        if(cqe->res < 0 || clients[req->client_idx].id != req->ID) { //if the ID is different then it means the connection has been freed already
          error = true;
          close_connection(req->client_idx);
        }else{
          error = false;
          if(cqe->res + req->written < req->total_length && cqe->res > 0){ //if the current request isn't finished, continue writing
            int rc = add_write_req_continued(req, cqe->res);
            req = nullptr; //we don't want to free the req yet
            if(rc == 0) break;
          }
          if(active_connections.count(req->client_idx) && clients[req->client_idx].id == req->ID){
            //the above will check specifically if the client is still valid, since in the case that
            //a new client joins immediately after old one leaves, they might get the same clients
            //array index, but the ID's would be different
            auto *queue_ptr = &clients[req->client_idx].send_data;
            queue_ptr->pop(); //remove the last processed item
            if(queue_ptr->size() > 0){ //if there's still some data in the queue, write it now
              const auto *buff = &queue_ptr->front().buff;
              add_write_req(req->client_idx, event_type::WRITE, (char*)&buff[0], buff->size()); //adds a plain HTTP write request
            }
          }
        }
        if(write_cb != nullptr) write_cb(req->client_idx, error, this, custom_obj); //call the write callback
        req->buffer = nullptr; //done with the request buffer, we pass a vector the the write function, automatic lifespan
        break;
      }
    }

    //free any malloc'd data
    if(req)
      free(req->buffer);
    free(req);

    io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
  }
}