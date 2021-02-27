#include "../header/server.h"
#include "../header/utility.h"

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
}

void server<server_type::NON_TLS>::write_connection(int client_idx, std::vector<char> &&buff) {
  auto *client = &clients[client_idx];
  client->send_data.emplace(std::move(buff));
  if(client->send_data.size() == 1){ //only adds a write request in the case that the queue was empty before this
    auto &data_ref = client->send_data.front();
    auto &buff = data_ref.buff;
    add_write_req(client_idx, event_type::WRITE, &buff[0], buff.size());
  }
}

void server<server_type::NON_TLS>::write_connection(int client_idx, char* buff, size_t length) {
  auto *client = &clients[client_idx];
  client->send_data.emplace(buff, length);
  if(client->send_data.size() == 1){ //only adds a write request in the case that the queue was empty before this
    auto &data_ref = client->send_data.front();
    auto &buff = data_ref.ptr_buff;
    add_write_req(client_idx, event_type::WRITE, buff, length);
  }
}

void server<server_type::NON_TLS>::close_connection(int client_idx) {
  auto &client = clients[client_idx];

  active_connections.erase(client_idx);
  client.send_data = {}; //free up all the data we might have wanted to send

  close(client.sockfd);

  freed_indexes.insert(client_idx);
}

template<typename U>
void server<server_type::NON_TLS>::broadcast_message(U begin, U end, int num_clients, std::vector<char> &&buff){
  if(num_clients > 0){
    auto data = new multi_write(std::move(buff), num_clients);

    for(auto client_idx_ptr = begin; client_idx_ptr != end; client_idx_ptr++){
      auto &client = clients[(int)*client_idx_ptr];
      client.send_data.emplace(data);
      if(client.send_data.size() == 1) //only adds a write request in the case that the queue was empty before this
        add_write_req(*client_idx_ptr, event_type::WRITE, &(data->buff[0]), data->buff.size());
    }
  }
}

template<typename U>
void server<server_type::NON_TLS>::broadcast_message(U begin, U end, int num_clients, char *buff, size_t length){
  if(num_clients > 0){
    for(auto client_idx_ptr = begin; client_idx_ptr != end; client_idx_ptr++){
      auto &client = clients[(int)*client_idx_ptr];
      client.send_data.emplace(buff, length);
      if(client.send_data.size() == 1) //only adds a write request in the case that the queue was empty before this
        add_write_req(*client_idx_ptr, event_type::WRITE, buff, length);
    }
  }
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

  add_tcp_accept_req();

  while(true){
    char ret = io_uring_wait_cqe(&ring, &cqe);
    if(ret < 0)
      fatal_error("io_uring_wait_cqe");
    request *req = (request*)cqe->user_data;

    std::cout << "event, non tls, " << thread_id << "\n";

    if(req->event != event_type::ACCEPT &&
      req->event != event_type::EVENTFD &&
      req->event != event_type::CUSTOM_READ &&
      cqe->res <= 0 &&
      clients[req->client_idx].id == req->ID
      )
    {
      if(close_cb != nullptr) close_cb(req->client_idx, this, custom_obj);
      close_connection(req->client_idx); //making sure to remove any data relating to it as well
    }else{
      switch(req->event){
        case event_type::ACCEPT: {
          add_tcp_accept_req();
          auto client_idx = setup_client(cqe->res);

          active_connections.insert(client_idx);
          //above basically says this connection is now active, checking if this connection replaced an existing but broken one happens elsewhere

          if(accept_cb != nullptr) accept_cb(client_idx, this, custom_obj);
          
          add_read_req(client_idx, event_type::READ); //also need to read whatever request it sends immediately
          break;
        }
        case event_type::READ: {
          if(read_cb != nullptr) read_cb(req->client_idx, &(req->read_data[0]), cqe->res, this, custom_obj);
          break;
        }
        case event_type::WRITE: {
          auto &client = clients[req->client_idx];
          if(cqe->res + req->written < req->total_length && cqe->res > 0){ //if the current request isn't finished, continue writing
            int rc = add_write_req_continued(req, cqe->res);
            req = nullptr; //we don't want to free the req yet
            if(rc == 0) break;
          }
          if(active_connections.count(req->client_idx) && client.id == req->ID){
            //the above will check specifically if the client is still valid, since in the case that
            //a new client joins immediately after old one leaves, they might get the same clients
            //array index, but the ID's would be different
            auto *queue_ptr = &client.send_data;
            queue_ptr->pop(); //remove the last processed item
            if(queue_ptr->size() > 0){ //if there's still some data in the queue, write it now
              auto &data_ref = queue_ptr->front();
              auto write_data_stuff = data_ref.get_ptr_and_size();
              add_write_req(req->client_idx, event_type::WRITE, write_data_stuff.buff, write_data_stuff.length); //adds a plain HTTP write request
            }
          }
          if(write_cb != nullptr) write_cb(req->client_idx, this, custom_obj); //call the write callback
          break;
        }
        case event_type::EVENTFD: {
          if(event_cb != nullptr) event_cb(this, custom_obj, std::move(req->read_data)); //call the event callback
          event_read(); //must be called to add another read request for the eventfd
          break;
        }
        case event_type::CUSTOM_READ: {
          if(req->read_data.size() == cqe->res + req->read_amount){
            if(custom_read_cb != nullptr) custom_read_cb(req->client_idx, (int)req->custom_info, std::move(req->read_data), this, custom_obj);
          }else{
            custom_read_req_continued(req, cqe->res);
            req = nullptr; //don't want it to be deleted yet
          }
          break;
        }
      }
    }

    delete req;

    io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
  }
}