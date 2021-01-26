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

void server<server_type::NON_TLS>::write_socket(int client_idx, std::vector<char> &&buff) {
  auto *client = &clients[client_idx];
  client->send_data.push(write_data(std::move(buff)));
  const auto data_ref = client->send_data.front();
  add_write_req(client_idx, event_type::WRITE, (char*)&data_ref.buff[0], data_ref.buff.size());
}

void server<server_type::NON_TLS>::close_socket(int client_idx) {
  auto *client = &clients[client_idx];

  active_connections.erase(client_idx);
  client->sockfd = 0;

  freed_indexes.push(client_idx);
}

int server<server_type::NON_TLS>::add_write_req_continued(request *req, int written) { //for long plain HTTP write requests, this writes at the correct offset
  req->written += written;
  
  io_uring_sqe *sqe = io_uring_get_sqe(&ring);
  io_uring_prep_write(sqe, req->client_socket, &req->buffer[req->written], req->total_length - req->written, 0); //do not write at an offset
  io_uring_sqe_set_data(sqe, req);
  io_uring_submit(&ring); //submits the event

  return 0;
}