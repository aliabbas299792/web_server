#include "../header/server.h"
#include "../header/utility.h"

#include <thread>

// define static stuff
std::vector<server<server_type::NON_TLS>*> server<server_type::NON_TLS>::non_tls_servers{};
std::mutex server<server_type::NON_TLS>::non_tls_server_vector_access{};

void server<server_type::NON_TLS>::kill_all_servers() {
  std::unique_lock<std::mutex> non_tls_access_lock(non_tls_server_vector_access);
  for(const auto server : non_tls_servers)
    server->kill_server();
}

server<server_type::NON_TLS>::server(
  int listen_port,
  void *custom_obj,
  accept_callback<server_type::NON_TLS> a_cb,
  close_callback<server_type::NON_TLS> c_cb,
  read_callback<server_type::NON_TLS> r_cb,
  write_callback<server_type::NON_TLS> w_cb,
  event_callback<server_type::NON_TLS> e_cb,
  custom_read_callback<server_type::NON_TLS> cr_cb
) : server_base<server_type::NON_TLS>(listen_port) { //call parent constructor with the port to listen on
  this->accept_cb = a_cb;
  this->close_cb = c_cb;
  this->read_cb = r_cb;
  this->write_cb = w_cb;
  this->event_cb = e_cb;
  this->custom_read_cb = cr_cb;
  this->custom_obj = custom_obj;

  std::unique_lock<std::mutex> access_lock(non_tls_server_vector_access);
  non_tls_servers.push_back(this); // basically so that anything which wants to manage all of the server at once, can
}

void server<server_type::NON_TLS>::write_connection(int client_idx, std::vector<char> &&buff) {
  auto &client = clients[client_idx];
  client.send_data.emplace(std::move(buff));
  if(client.send_data.size() == 1){ //only adds a write request in the case that the queue was empty before this
    auto &data_ref = client.send_data.front();
    auto &buff = data_ref.buff;
    add_write_req(client_idx, event_type::WRITE, &buff[0], buff.size());
  }
}

void server<server_type::NON_TLS>::write_connection(int client_idx, char* buff, size_t length) {
  auto &client = clients[client_idx];
  client.send_data.emplace(buff, length);
  if(client.send_data.size() == 1){ //only adds a write request in the case that the queue was empty before this
    auto &data_ref = client.send_data.front();
    auto &buff = data_ref.ptr_buff;
    add_write_req(client_idx, event_type::WRITE, buff, length);
  }
}

void server<server_type::NON_TLS>::close_connection(int client_idx) {
  auto &client = clients[client_idx];

  if(client.num_write_reqs == 0){ // only erase this client if they haven't got any active write requests
    active_connections.erase(client_idx);
    client.send_data = {}; //free up all the data we might have wanted to send

    close(client.sockfd);

    freed_indexes.insert(client_idx);
  }
}

int server<server_type::NON_TLS>::add_write_req_continued(request *req, int written) { //for long plain HTTP write requests, this writes at the correct offset
  auto &client = clients[req->client_idx];
  auto data = client.send_data.front().get_ptr_and_size();

  req->written += written;
  
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_write(sqe, client.sockfd, &data.buff[req->written], req->total_length - req->written, 0); //do not write at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event
  return 0;
}

void server<server_type::NON_TLS>::req_event_handler(request *&req, int cqe_res){
  switch(req->event){
    case event_type::ACCEPT: {
      add_tcp_accept_req();
      auto client_idx = setup_client(cqe_res);

      active_connections.insert(client_idx);
      //above basically says this connection is now active, checking if this connection replaced an existing but broken one happens elsewhere

      if(accept_cb != nullptr) accept_cb(client_idx, this, custom_obj);
      
      add_read_req(client_idx, event_type::READ); //also need to read whatever request it sends immediately
      break;
    }
    case event_type::READ: {
      clients[req->client_idx].read_req_active = false;
      if(read_cb != nullptr) read_cb(req->client_idx, &(req->read_data[0]), cqe_res, this, custom_obj);
      break;
    }
    case event_type::WRITE: {
      int broadcast_additional_info = -1; // only used for broadcast messages
      auto &client = clients[req->client_idx];
      if(cqe_res + req->written < req->total_length && cqe_res > 0){ //if the current request isn't finished, continue writing
        int rc = add_write_req_continued(req, cqe_res);
        req = nullptr; //we don't want to free the req yet
        if(rc == 0) break;
      }
      client.num_write_reqs--; // decrement number of active write requests
      if(active_connections.count(req->client_idx) && client.id == req->ID){
        //the above will check specifically if the client is still valid, since in the case that
        //a new client joins immediately after old one leaves, they might get the same clients
        //array index, but the ID's would be different
        auto *queue_ptr = &client.send_data;

        if(queue_ptr->front().broadcast) //if it's broadcast, then custom_info must be the item_idx
          broadcast_additional_info = queue_ptr->front().custom_info;

        queue_ptr->pop(); //remove the last processed item
        if(queue_ptr->size() > 0){ //if there's still some data in the queue, write it now
          auto &data_ref = queue_ptr->front();
          auto write_data_stuff = data_ref.get_ptr_and_size();
          add_write_req(req->client_idx, event_type::WRITE, write_data_stuff.buff, write_data_stuff.length); //adds a plain HTTP write request
        }
      }
      if(write_cb != nullptr) write_cb(req->client_idx, broadcast_additional_info, this, custom_obj); //call the write callback
      break;
    }
  }
}