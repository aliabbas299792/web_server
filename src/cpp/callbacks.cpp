#include "../header/callbacks.h"
#include "../header/utility.h"

#include <openssl/sha.h>
#include <openssl/evp.h>

void a_cb(int client_fd, server *tcp_server, void *custom_obj){ //the accept callback
  // std::cout << "Accepted new connection: " << client_fd << "\n";
}

std::string get_accept_header_value(std::string input) {
  input += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"; //concatenate magic string
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((const unsigned char*)input.c_str(), input.size(), hash);
  //for input of length 20, base64 output length is 28 (a bit over *4/3 - *4/3 for larger lengths)
  auto base64data = (char*)calloc(28+1, 1); //+1 for the terminating null that EVP_EncodeBlock adds on
  EVP_EncodeBlock((unsigned char*)base64data, hash, SHA_DIGEST_LENGTH);
  return base64data;
}

receiving_data_info websocket_frame_info(std::vector<uchar> buffer){
  int offset = 0;
  ulong length = buffer[1] & 0x7f;
  if(length == 126){
    offset = 2;
    length = ntohs(*((u_short*)&buffer[2]));
  }else if(length == 127){
    offset = 8;
    length = ntohs(*((u_long*)&buffer[2]));
  }

  return receiving_data_info(length + 6 + offset, buffer); //basically the entire request hasn't actually been received yet
}

std::pair<int, std::vector<uchar>> decode_websocket_frame(std::vector<uchar> data){
  const uint fin = ((int)data[0] & 0x80) == 0x80;
  const uint opcode = (int)data[0] & 0xf;
  const uint mask = ((int)data[1] & 0x80) == 0x80;

  if(!mask) return {-1, {}};

  int offset = 0;

  ulong length = data[1] & 0x7f;
  if(length == 126){
    offset = 2;
    length = ntohs(*((u_short*)&data[2]));
  }else if(length == 127){
    offset = 8;
    length = ntohs(*((u_long*)&data[2]));
  }
  
  if(data.size() - 6 - offset != length) return {length, data}; //basically the entire request hasn't actually been received yet

  const std::vector<uchar> masking_key{ data[2+offset], data[3+offset], data[4+offset], data[5+offset] };

  std::vector<uchar> decoded{};
  for(int i = 6+offset; i < data.size(); i++){
    decoded.push_back(data[i] ^ masking_key[(i - (6 + offset)) % 4]);
  }

  if(!fin) return {-2, decoded};
  
  return {-3, decoded};
}

void r_cb(int client_fd, char *buffer, unsigned int length, server *tcp_server, void *custom_obj){
  std::vector<std::string> headers;
  const auto basic_web_server = (web_server*)custom_obj;

  bool accept_bytes = false;
  std::string sec_websocket_key = "";

  const auto websocket_key_token = "Sec-WebSocket-Key: ";

  std::vector<uchar> buff_vec{};
  buff_vec.insert(buff_vec.end(), buffer, buffer + length);

  char *str = nullptr;
  char *saveptr = nullptr;
  while((str = strtok_r(((char*)buffer), "\r\n", &saveptr))){ //retrieves the headers
    std::string tempStr = std::string(str, strlen(str));
    
    if(tempStr.find("Range: bytes=") != std::string::npos) accept_bytes = true;
    if(tempStr.find("Sec-WebSocket-Key") != std::string::npos)
      sec_websocket_key = tempStr.substr(strlen(websocket_key_token));
    buffer = nullptr;
    headers.push_back(tempStr);
  }

  if(sec_websocket_key != ""){ //websocket connection callback
    std::cout << "Test: " << get_accept_header_value("dGhlIHNhbXBsZSBub25jZQ==").size() << "\n";

    const std::string accept_header_value = get_accept_header_value(sec_websocket_key);
    std::cout << accept_header_value.size() << " " << sec_websocket_key << "\n";
    const auto resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept_header_value + "\r\n\r\n";

    std::vector<char> send_buffer(resp.size());
    std::memcpy(&send_buffer[0], resp.c_str(), resp.size());

    tcp_server->write_socket(client_fd, std::move(send_buffer));
    basic_web_server->websocket_connections.insert(client_fd);
  } else if(!strcmp(strtok_r((char*)headers[0].c_str(), " ", &saveptr), "GET")){ //get callback
    char *path = strtok_r(nullptr, " ", &saveptr);
    std::string processed_path = std::string(&path[1], strlen(path)-1);
    processed_path = processed_path == "" ? "public/index.html" : "public/"+processed_path;

    char *http_version = strtok_r(nullptr, " ", &saveptr);
    
    std::vector<char> send_buffer{};
    
    if((send_buffer = basic_web_server->read_file_web(processed_path, 200, accept_bytes)).size() != 0){
      tcp_server->write_socket(client_fd, std::move(send_buffer));
    }else{
      send_buffer = basic_web_server->read_file_web("public/404.html", 400);
      tcp_server->write_socket(client_fd, std::move(send_buffer));
    }
  } else { //if nothing else, then just add in another read request for this socket, since we're not writing
    auto frame_info = websocket_frame_info(buff_vec);

    if(frame_info.buffer.size() == frame_info.length){ //exactly how much was needed
      std::cout << "one shot exactly enough\n";
    }else if(basic_web_server->receiving_data.count(client_fd)){ //if there is already some pending data
      const auto *pending_item = &basic_web_server->receiving_data[client_fd];
      if(frame_info.buffer.size() + pending_item->buffer.size() == pending_item->length){
        std::cout << "multiple receives exactly enough\n";
      }else if(frame_info.buffer.size() + pending_item->buffer.size() < pending_item->length){ //too little data
        auto *vec_member = &basic_web_server->receiving_data[client_fd].buffer;
        vec_member->insert(vec_member->end(), std::make_move_iterator(frame_info.buffer.begin()), std::make_move_iterator(frame_info.buffer.end()) ); 
      }else{ //too much data
        auto *recvd_info = &basic_web_server->receiving_data[client_fd];
        const auto required_length = recvd_info->length - recvd_info->buffer.size();
        recvd_info->buffer.insert(recvd_info->buffer.end(), std::make_move_iterator(frame_info.buffer.begin()), std::make_move_iterator(frame_info.buffer.begin() + required_length) );
        remove_first_n_elements(recvd_info->buffer, required_length);
      }
    }else{ //if there is no pending data, make this pending
      auto *pending_item = &basic_web_server->receiving_data[client_fd];
      pending_item->length = frame_info.length;
      pending_item->buffer.insert(pending_item->buffer.end(), std::make_move_iterator(frame_info.buffer.begin()), std::make_move_iterator(frame_info.buffer.end()) ); 
    }

    /*auto processed_data = decode_websocket_frame(buff_vec);

    if(processed_data.first != -3){
      auto *vec_member = &basic_web_server->receiving_data[client_fd];
      vec_member->insert(vec_member->end(), std::make_move_iterator(processed_data.second.begin()), std::make_move_iterator(processed_data.second.end()) );
    }

    if(processed_data.first == -2){
      //fatal_error("Mask bit not sent for socket...");
    }else if(processed_data.first == -1 || processed_data.first == -3){
      auto *vec_member = &basic_web_server->data_store[client_fd];
      vec_member->insert(vec_member->end(), std::make_move_iterator(processed_data.second.begin()), std::make_move_iterator(processed_data.second.end()) );
    }else{
      //basically make it receive the entire message and add it to a queue if it's not final etc etc
    }
    
    std::cout << "first: " << processed_data.first << "\n";
    std::cout << "length: " << buff_vec.size() << "\n\n\n";
    if(processed_data.first == -3){
      //std::cout << std::string((char*)(&processed_data.second[0]), processed_data.second.size()) << "\n";
      basic_web_server->data_store.erase(client_fd);
    }

    */
    if(basic_web_server->websocket_connections.count(client_fd))
      tcp_server->read_socket(client_fd);
  }
}

void w_cb(int client_fd, server *tcp_server, void *custom_obj){
  const auto basic_web_server = (web_server*)custom_obj;
  // std::cout << "write_socket callback\n";
  if(basic_web_server->websocket_connections.count(client_fd))
    tcp_server->read_socket(client_fd);
  else
    tcp_server->close_socket(client_fd); //for web requests you close the socket right after
}