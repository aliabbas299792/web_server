#include "../header/callbacks.h"
#include "../header/utility.h"

#include <openssl/sha.h>
#include <openssl/evp.h>

template<server_type T>
void a_cb(int client_idx, server<T> *tcp_server, void *custom_obj){ //the accept callback
  // std::cout << "Accepted new connection: " << client_idx << "\n";
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

ulong get_ws_frame_length(const char *buffer){
  ulong packet_length = buffer[1] & 0x7f;
  if(packet_length == 126){
    packet_length = ntohs(*((u_short*)&((uchar*)buffer)[2])) + 2; //the 2 bytes extra needed to store the length are added
  }else if(packet_length == 127){
    packet_length = be64toh(*((u_long*)&((uchar*)buffer)[2])) + 8; //the 8 bytes extra needed to store the length are added, be64toh used because ntohl is 32 bit
  }
  return packet_length + 6; // +6 bytes for the header data
}

std::vector<char> make_ws_frame(std::string packet_msg, websocket_non_control_opcodes opcode){
  //gets the correct offsets and sizes
  int offset = 2; //first 2 bytes for the header data (excluding the extended length bit)
  uchar payload_len_char = 0;
  ushort payload_len_short = 0;
  ulong payload_len_long = 0;
  const auto msg_size = packet_msg.size();
  if(msg_size < 126){ //less than 126 bytes long
    payload_len_char = msg_size;
  }else if(msg_size < 65536){ //less than 2^16 bytes long
    offset += 2;
    payload_len_short = htons(msg_size);
  }else{ //more than 2^16 bytes long
    offset += 8;
    payload_len_long = htobe64(msg_size);
  }
  
  std::vector<char> data(offset + msg_size);
  data[0] = 128 | opcode; //not gonna do fragmentation, so set the fin bit, and the opcode
  //don't mask frames being sent to the client

  //sets the correct payload length
  if(payload_len_char)
    data[1] = payload_len_char;
  else if(payload_len_short){
    data[1] = 126;
    *((ushort*)&data[2]) = payload_len_short;
  }else{
    data[1] = 127;
    *((ulong*)&data[2]) = payload_len_long;
  }
  
  std::memcpy(&data[offset], packet_msg.c_str(), msg_size);

  return data;
}

template<server_type T>
bool close_ws_connection_req(int client_idx, server<T> *tcp_or_tls_server, web_server *basic_web_server, bool client_already_closed = false){
  basic_web_server->close_pending_ops_map[client_idx]++;
  basic_web_server->websocket_connections.erase(client_idx);
  basic_web_server->websocket_frames.erase(client_idx);
  if(!client_already_closed) {
    auto data = make_ws_frame("", websocket_non_control_opcodes::close_connection);
    tcp_or_tls_server->write_connection(client_idx, std::move(data));
  }
  return true;
}

template<server_type T>
bool close_ws_connection_confirm(int client_idx, server<T> *tcp_or_tls_server, web_server *basic_web_server){
  if(!(basic_web_server->close_pending_ops_map[client_idx] - 1)){
    basic_web_server->close_pending_ops_map.erase(client_idx);
    tcp_or_tls_server->close_connection(client_idx);
  }else{
    basic_web_server->close_pending_ops_map[client_idx]--;
  }
  return true;
}

std::pair<int, std::vector<uchar>> decode_websocket_frame(std::vector<uchar> data){
  const uint fin = (data[0] & 0x80) == 0x80;
  const uint opcode = data[0] & 0xf;
  const uint mask = (data[1] & 0x80) == 0x80;

  if(!mask) return {-1, {}}; //mask must be set
  if(opcode == 0x8) return {-3, {}}; //the close opcode
  if(opcode == 0xA) return {3, {}}; //the pong opcode

  int offset = 0;

  ulong length = data[1] & 0x7f;
  if(length == 126){
    offset = 2;
    length = ntohs(*((u_short*)&data[2]));
  }else if(length == 127){
    offset = 8;
    length = ntohs(*((u_long*)&data[2]));
  }

  const std::vector<uchar> masking_key{ data[2+offset], data[3+offset], data[4+offset], data[5+offset] };

  std::vector<uchar> decoded{};
  for(int i = 6+offset; i < data.size(); i++){
    decoded.push_back(data[i] ^ masking_key[(i - (6 + offset)) % 4]);
  }

  if(opcode == 0x9) return {2, decoded}; //the ping opcode
  if(!fin) return {-2, decoded}; //fin bit not set, so put this in a pending larger buffer of decoded data
  
  return {1, decoded}; //succesfully decoded, and is the final frame
}

template<server_type T>
std::pair<int, std::vector<std::vector<uchar>>> get_ws_frames(char *buffer, int length, int client_idx, web_server *basic_web_server, server<T> *tcp_or_tls_server){
  std::vector<std::vector<uchar>> frames;

  int remaining_length = length;

  if(basic_web_server->receiving_data.count(client_idx)){ //if there is already pending data
    auto *pending_item = &basic_web_server->receiving_data[client_idx];

    if(pending_item->length == -1){ //assuming the received buffer and the pending buffer are at least 10 bytes long
      if(pending_item->buffer.size() + length < 10){
        // std::cout << "super small read...\n";
        close_ws_connection_req(client_idx, tcp_or_tls_server, basic_web_server); //doesn't deal with such small reads - just close the connection
        return {-1, {}};
      }else{
        std::vector<char> temp_buffer(10); //we only need first 10 bytes for length
        temp_buffer.insert(temp_buffer.begin(), pending_item->buffer.begin(), pending_item->buffer.end());
        temp_buffer.insert(temp_buffer.begin(), buffer, buffer + ( 10 - pending_item->buffer.size() ));
        pending_item->length = get_ws_frame_length(&temp_buffer[0]);
      }
    }
    
    const auto required_length = pending_item->length - pending_item->buffer.size();

    if(required_length < length){ //more than enough data
      remaining_length = length - required_length;
      pending_item->buffer.insert(pending_item->buffer.end(), buffer, buffer + required_length);
      frames.push_back(std::move(pending_item->buffer));

      char *remaining_data = nullptr;
      remove_first_n_elements(buffer, (int)length, remaining_data, (int)required_length);
      buffer = remaining_data;
      basic_web_server->receiving_data.erase(client_idx);
    
    }else if(required_length == length){ //just enough data
      remaining_length = 0; //we've used up all of the data
      pending_item->buffer.insert(pending_item->buffer.end(), buffer, buffer + length);
      frames.push_back(std::move(pending_item->buffer));
      basic_web_server->receiving_data.erase(client_idx);

    }else{ //too little data
      remaining_length = 0; //we've used up all of the data
      pending_item->buffer.insert(pending_item->buffer.end(), buffer, buffer + length);
    
    }

  }
  
  //by this point, the data in the buffer is guaranteed to start with a new frame
  while(remaining_length > 10){ //you only loop while greater than 10 bytes, since under 10 bytes may not have enough data to get the length
    auto packet_length = get_ws_frame_length(buffer);
    
    if(remaining_length >= packet_length){
      frames.push_back(std::vector<uchar>(buffer, buffer + packet_length));
      char *remaining_data = nullptr;
      remove_first_n_elements(buffer, (int)remaining_length, remaining_data, (int)packet_length);
      buffer = remaining_data;
      remaining_length -= packet_length;
    }else{
      break;
    }
  }

  //by this point only the beginnings of frames should be left, if the remaining_length is not 0
  if(remaining_length > 0){
    auto *pending_item = &basic_web_server->receiving_data[client_idx];
    pending_item->buffer = std::vector<uchar>(buffer, buffer + remaining_length);
    if(remaining_length > 10){
      pending_item->length = get_ws_frame_length(buffer);
    }else{
      pending_item->length = -1;
    }
  }

  return {1, frames};
}

bool is_valid_http_req(const char* buff, int length){
  if(length < 16) return false; //minimum size for valid HTTP request is 16 bytes
  const char *types[] = { "GET ", "POST ", "PUT ", "DELETE ", "PATCH " };
  u_char valid = 0x1f;
  for(int i = 0; i < 7; i++) //length of "DELETE " is 7 characters
    for(int j = 0; j < 5; j++) //5 different types
      if(i < strlen(types[j]) && (valid >> j) & 0x1 && types[j][i] != buff[i]) valid &= 0x1f ^ (1 << j);
  return valid;
}

template<server_type T>
void r_cb(int client_idx, char *buffer, unsigned int length, server<T> *tcp_or_tls_server, void *custom_obj){
  const auto basic_web_server = (web_server*)custom_obj;

  if(is_valid_http_req(buffer, length)){ //if not a valid HTTP req, then probably a websocket frame
    std::vector<std::string> headers;

    bool accept_bytes = false;
    std::string sec_websocket_key = "";

    const auto websocket_key_token = "Sec-WebSocket-Key: ";

    char *str = nullptr;
    char *saveptr = nullptr;
    char *buffer_str = buffer;
    while((str = strtok_r(((char*)buffer_str), "\r\n", &saveptr))){ //retrieves the headers
      std::string tempStr = std::string(str, strlen(str));
      
      if(tempStr.find("Range: bytes=") != std::string::npos) accept_bytes = true;
      if(tempStr.find("Sec-WebSocket-Key") != std::string::npos)
        sec_websocket_key = tempStr.substr(strlen(websocket_key_token));
      buffer_str = nullptr;
      headers.push_back(tempStr);
    }

    if(sec_websocket_key != ""){ //websocket connection callback
      const std::string accept_header_value = get_accept_header_value(sec_websocket_key);
      const auto resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept_header_value + "\r\n\r\n";

      std::vector<char> send_buffer(resp.size());
      std::memcpy(&send_buffer[0], resp.c_str(), resp.size());

      tcp_or_tls_server->write_connection(client_idx, std::move(send_buffer));
      basic_web_server->websocket_connections.insert(client_idx);
    } else if(!strcmp(strtok_r((char*)headers[0].c_str(), " ", &saveptr), "GET")){ //get callback
      char *path = strtok_r(nullptr, " ", &saveptr);
      std::string processed_path = std::string(&path[1], strlen(path)-1);
      processed_path = processed_path == "" ? "public/index.html" : "public/"+processed_path;

      char *http_version = strtok_r(nullptr, " ", &saveptr);
      
      std::vector<char> send_buffer{};
      
      if((send_buffer = basic_web_server->read_file_web(processed_path, 200, accept_bytes)).size() != 0){
        tcp_or_tls_server->write_connection(client_idx, std::move(send_buffer));
      }else{
        send_buffer = basic_web_server->read_file_web("public/404.html", 400);
        tcp_or_tls_server->write_connection(client_idx, std::move(send_buffer));
      }
    } 
  } else if(basic_web_server->websocket_connections.count(client_idx)) { //this bit should be just websocket frames
    std::vector<std::vector<uchar>> frames{};
    auto frame_pair = get_ws_frames(buffer, length, client_idx, basic_web_server, tcp_or_tls_server);

    if(frame_pair.first == 1){ //if frame_pair.first == -1, then we're trying to immediately close
      frames = std::move(frame_pair.second);
      //by this point the frame has definitely been fully received

      std::vector<uchar> frame_contents{};
      bool closed = false;
      
      for(const auto &frame : frames){
        if(frame.size()){
          auto processed_data = decode_websocket_frame(frame);

          frame_contents.clear();
          //for now finishes and prints last 200 bytes
          if(processed_data.first == 1){ // 1 is to indicate that it's done
            if(basic_web_server->websocket_frames.count(client_idx)){
              auto *vec_member = &basic_web_server->websocket_frames[client_idx];
              vec_member->insert(vec_member->end(), processed_data.second.begin(), processed_data.second.end());
              frame_contents = std::move(*vec_member);
              basic_web_server->websocket_frames.erase(client_idx);
            }else{
              frame_contents = std::move(processed_data.second);
            }
          }else if(processed_data.first == -2){
            auto *vec_member = &basic_web_server->websocket_frames[client_idx];
            vec_member->insert(vec_member->begin(), processed_data.second.begin(), processed_data.second.end());
          }else if(processed_data.first == -3){ //close opcode
            basic_web_server->close_pending_ops_map[client_idx]++;
            //we're going to close immediately after, so make sure the program knows there is this write op happening
            closed = close_ws_connection_req(client_idx, tcp_or_tls_server, basic_web_server, true);
          }else if(processed_data.first == 2){ //ping opcode
            std::string body_data((const char*)&processed_data.second[0], processed_data.second.size());
            auto data = make_ws_frame(body_data, websocket_non_control_opcodes::pong);
            tcp_or_tls_server->write_connection(client_idx, std::move(data));
          }
        }

        if(frame_contents.size() > 0){
          //this is where you'd deal with websocket connections
          std::string str = "";
          for(int i = 0; i < 1024*1024*5; i++){
            str += "A";
          }

          str += " ... hello world.";

          auto data = make_ws_frame(str, websocket_non_control_opcodes::binary_frame); //echos back whatever you send
          tcp_or_tls_server->write_connection(client_idx, std::move(data));
          
          basic_web_server->close_pending_ops_map[client_idx]++;
          //we're going to close immediately after, so make sure the program knows there is this write op happening
          closed = close_ws_connection_req(client_idx, tcp_or_tls_server, basic_web_server);
        }

        if(closed)
          break;
      }

      if(!closed)
        tcp_or_tls_server->read_connection(client_idx); //since it's a websocket, add another read request right after
    }
  }
}

template<server_type T>
void force_shut_ws_connection(int client_idx, server<T> *tcp_or_tls_server, web_server *basic_web_server){
  basic_web_server->close_pending_ops_map.erase(client_idx);
  basic_web_server->websocket_connections.erase(client_idx);
  basic_web_server->websocket_frames.erase(client_idx);
  tcp_or_tls_server->close_connection(client_idx);
}

template<server_type T>
void w_cb(int client_idx, bool error, server<T> *tcp_or_tls_server, void *custom_obj){
  const auto basic_web_server = (web_server*)custom_obj;

  if(error) //if there was an error then clean up and force shutdown
    force_shut_ws_connection(client_idx, tcp_or_tls_server, basic_web_server);
  else if(basic_web_server->close_pending_ops_map.count(client_idx) || error){
    close_ws_connection_confirm(client_idx, tcp_or_tls_server, basic_web_server);
  }else{
    if(basic_web_server->websocket_connections.count(client_idx)){
      tcp_or_tls_server->read_connection(client_idx);
    }else{
      tcp_or_tls_server->close_connection(client_idx); //for web requests you close the connection right after
    }
  }
}