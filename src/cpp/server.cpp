#include "../header/server.h"
#include "../header/utility.h"


























// void server::serverLoop(){
//   running_server = true;

//   io_uring_cqe *cqe;
//   sockaddr_storage client_address;
//   socklen_t client_address_length = sizeof(client_address);

//   add_accept_req(listener_fd, &client_address, &client_address_length);

//   while(true){
//     char ret = io_uring_wait_cqe(&ring, &cqe);
//     if(ret < 0)
//       fatal_error("io_uring_wait_cqe");
//     request *req = (request*)cqe->user_data;
    
//     switch(req->event){
//       case event_type::ACCEPT: {
//         add_accept_req(listener_fd, &client_address, &client_address_length);
//         if(is_tls) {
//           tls_accept(cqe->res);
//         }else{
//           if(a_cb != nullptr) a_cb(cqe->res, this, custom_obj);
//           add_read_req(cqe->res); //also need to read whatever request it sends immediately
//         }
//         req->buffer = nullptr; //done with the request buffer
//         break;
//       }
//       case event_type::READ: {
//         if(cqe->res > 0)
//           if(r_cb != nullptr) r_cb(req->client_socket, req->buffer, cqe->res, this, custom_obj);
//         break;
//       }
//       case event_type::WRITE: {
//         std::cout << "res: " << cqe->res << "\n";
//         std::cout << (ulong)req->buffer << " == " << (ulong)&(send_data[req->client_socket].front().buff[0]) << "\n";
//         if(cqe->res + req->written < req->total_length && cqe->res > 0){ //if the current request isn't finished, continue writing
//           int rc = add_write_req_continued(req, cqe->res);
//           req = nullptr; //we don't want to free the req yet
//           if(rc == 0) break;
//         }
//         if(send_data.count(req->client_socket)){
//           auto *queue_ptr = &send_data[req->client_socket];
//           queue_ptr->pop(); //remove the last processed item
//           if(queue_ptr->size() > 0){ //if there's still some data in the queue, write it now
//             const auto *buff = &queue_ptr->front().buff;
//             add_write_req(req->client_socket, (char*)&buff[0], buff->size()); //adds a plain HTTP write request
//           }
//         }
//         if(w_cb != nullptr) w_cb(req->client_socket, this, custom_obj); //call the write callback
//         req->buffer = nullptr; //done with the request buffer, we pass a vector the the write function, automatic lifespan
//         break;
//       }
//       case event_type::ACCEPT_READ_SSL: {
//         if(cqe->res > 0 && socket_to_ssl.count(req->client_socket)) { //if an error occurred, don't try to negotiate the connection
//           auto ssl = socket_to_ssl[req->client_socket];
//           if(!recv_data.count(req->client_socket)){ //if there is no data in the map, add it
//             recv_data[req->client_socket] = std::vector<char>(req->buffer, req->buffer + cqe->res);
//           }else{ //otherwise copy the new data to the end of the old data
//             auto *buffer = &recv_data[req->client_socket];
//             buffer->insert(buffer->end(), req->buffer, req->buffer + cqe->res);
//           }
//           if(wolfSSL_accept(ssl) == 1){ //that means the connection was successfully established
//             if(a_cb != nullptr) a_cb(req->client_socket, this, custom_obj);
//             active_connections.insert(req->client_socket);

//             std::vector<char> buffer(READ_SIZE);
//             auto amount_read = wolfSSL_read(socket_to_ssl[req->client_socket], &buffer[0], READ_SIZE);
//             //above will either add in a read request, or get whatever is left in the local buffer (as we might have got the HTTP request with the handshake)

//             recv_data.erase(req->client_socket);
//             if(amount_read > -1)
//               if(r_cb != nullptr) r_cb(req->client_socket, &buffer[0], amount_read, this, custom_obj);
//           }
//         }else{
//           close_socket(req->client_socket); //making sure to remove any data relating to it as well
//         }
//         break;
//       }
//       case event_type::ACCEPT_WRITE_SSL: { //used only for when wolfSSL needs to write data during the TLS handshake
//         if(cqe->res <= 0 || !socket_to_ssl.count(req->client_socket)) { //if an error occurred, don't try to negotiate the connection
//           close_socket(req->client_socket); //making sure to remove any data relating to it as well
//         }else{
//           accept_send_data[req->client_socket] = cqe->res; //this is the amount that was last written, used in the tls_write callback
//           wolfSSL_accept(socket_to_ssl[req->client_socket]); //call accept again
//         }
//         req->buffer = nullptr; //done with the request buffer
//         break;
//       }
//       case event_type::WRITE_SSL: { //used for generally writing over TLS
//         if(cqe->res > 0 && socket_to_ssl.count(req->client_socket) && send_data.count(req->client_socket) && send_data[req->client_socket].size() > 0){ //ensure this connection is still active
//           auto *data_ref = &send_data[req->client_socket].front();
//           data_ref->last_written = cqe->res;
//           int written = wolfSSL_write(socket_to_ssl[req->client_socket], &(data_ref->buff[0]), data_ref->buff.size());
//           if(written > -1){ //if it's not negative, it's all been written, so this write call is done
//             send_data[req->client_socket].pop();
//             if(w_cb != nullptr) w_cb(req->client_socket, this, custom_obj);
//             if(send_data[req->client_socket].size()){ //if the write queue isn't empty, then write that as well
//               data_ref = &send_data[req->client_socket].front();
//               wolfSSL_write(socket_to_ssl[req->client_socket], &(data_ref->buff[0]), data_ref->buff.size());
//             }
//           }
//         }else{
//           close_socket(req->client_socket); //otherwise make sure that the socket is closed properly
//         }
//         req->buffer = nullptr; //done with the request buffer
//         break;
//       }
//       case event_type::READ_SSL: { //used for reading over TLS
//         if(cqe->res > 0 && socket_to_ssl.count(req->client_socket)){
//           int to_read_amount = cqe->res; //the default read size
//           if(recv_data.count(req->client_socket)){ //will correctly deal with needing to call wolfSSL_read multiple times
//             auto *vec_member = &recv_data[req->client_socket];
//             vec_member->insert(vec_member->end(), req->buffer, req->buffer + cqe->res);
//             to_read_amount = vec_member->size(); //the read amount has got to be bigger, since the pending data could be more than READ_SIZE
//           }else{
//             recv_data[req->client_socket] = std::vector<char>(req->buffer, req->buffer + cqe->res);
//           }

//           std::vector<char> buffer(to_read_amount);
//           int total_read = 0;

//           while(recv_data[req->client_socket].size()){
//             int this_time = wolfSSL_read(socket_to_ssl[req->client_socket], &buffer[total_read], to_read_amount - total_read);
//             if(this_time <= 0) break;
//             total_read += this_time;
//           }

//           if(total_read == 0) add_read_req(req->client_socket); //total_read of 0 implies that data must be read into the recv_data buffer
          
//           if(total_read > 0){
//            if(r_cb != nullptr) r_cb(req->client_socket, &buffer[0], total_read, this, custom_obj);
//             if(!recv_data[req->client_socket].size())
//               recv_data.erase(req->client_socket);
//           }
//         }else{
//           close_socket(req->client_socket);
//         }
//         break;
//       }
//     }

//     //free any malloc'd data
//     if(req)
//       free(req->buffer);
//     free(req);

//     io_uring_cqe_seen(&ring, cqe); //mark this CQE as seen
//   }
// }