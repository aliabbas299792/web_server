#pragma once
#include "../header/web_server.h"

#include <openssl/sha.h>
#include <openssl/evp.h>

template<server_type T>
void web_server<T>::websocket_accept_read_cb(const std::string& sec_websocket_key, const std::string &path, int client_idx){
  const std::string accept_header_value = get_accept_header_value(sec_websocket_key);
  const auto resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept_header_value + "\r\n\r\n";

  std::vector<char> send_buffer(resp.size());
  std::memcpy(&send_buffer[0], resp.c_str(), resp.size());

  int ws_client_idx = new_ws_client(client_idx); //sets this index up as a new client
  
  tcp_server->write_connection(client_idx, std::move(send_buffer), ws_client_idx);
}

template<server_type T>
void web_server<T>::websocket_process_read_cb(int ws_client_idx, char *buffer, int length){ //we assume that the tcp server has been set by this point
  std::vector<std::vector<uchar>> frames{};
  auto frame_pair = get_ws_frames(buffer, length, ws_client_idx);

  auto client_idx = websocket_clients[ws_client_idx].client_idx;
  auto &client_data = websocket_clients[ws_client_idx];

  bool closed = false;
  if(frame_pair.first == 1){ //if frame_pair.first == -1, then we're trying to immediately close
    frames = std::move(frame_pair.second);
    //by this point the frame has definitely been fully received

    std::vector<uchar> frame_contents{};
    
    for(const auto &frame : frames){
      if(frame.size()){
        auto processed_data = decode_websocket_frame(frame);
        
        frame_contents.clear();
        //for now finishes and prints last 200 bytes
        if(processed_data.first == 1){ // 1 is to indicate that it's done
          if(client_data.websocket_frames.size() > 0){
            auto *vec_member = &client_data.websocket_frames;
            vec_member->insert(vec_member->end(), processed_data.second.begin(), processed_data.second.end());
            frame_contents = std::move(*vec_member);
            client_data.websocket_frames = {};
          }else{
            frame_contents = std::move(processed_data.second);
          }
        }else if(processed_data.first == -2){
          auto *vec_member = &client_data.websocket_frames;
          vec_member->insert(vec_member->begin(), processed_data.second.begin(), processed_data.second.end());
        }else if(processed_data.first == -3){ //close opcode
          client_data.currently_writing++;
          //we're going to close immediately after, so make sure the program knows there is this write op happening
          closed = close_ws_connection_req(ws_client_idx);
        }else if(processed_data.first == 2){ //ping opcode
          std::string body_data((const char*)&processed_data.second[0], processed_data.second.size());
          auto data = make_ws_frame(body_data, websocket_non_control_opcodes::pong);
          websocket_write(ws_client_idx, std::move(data));
        }
      }

      if(frame_contents.size() > 0){
        //this is where you'd deal with websocket connections
        std::string str = "";
        for(int i = 0; i < 1024*1024*20; i++){
          str += "A";
        }

        str += " ... hello world.";

        auto data = make_ws_frame(str, websocket_non_control_opcodes::binary_frame); //echos back whatever you send
        websocket_write(ws_client_idx, std::move(data));

        //we're going to close immediately after, so make sure the program knows there is this write op happening
        // closed = close_ws_connection_req(ws_client_idx);
      }

      if(closed)
        break;
    }
  }

  if(!closed)
    tcp_server->read_connection(client_idx, ws_client_idx); //since it's a websocket, add another read request right after
}

template<server_type T>
bool web_server<T>::websocket_process_write_cb(int ws_client_idx){
  if(all_websocket_connections.count(ws_client_idx)){
    close_ws_connection_potential_confirm(ws_client_idx);
    return true;
  }
  return false;
}

template<server_type T>
void web_server<T>::websocket_write(int ws_client_idx, std::vector<char> &&buff){
  auto &client_data = websocket_clients[ws_client_idx];
  client_data.currently_writing++;
  tcp_server->write_connection(client_data.client_idx, std::move(buff), ws_client_idx);
}

template<server_type T>
int web_server<T>::new_ws_client(int client_idx){
  auto index = 0;

  if(freed_indexes.size()){ //if there's a free index, give that
    index = *freed_indexes.begin(); //get first element in set
    freed_indexes.erase(index); //erase first element in set

    auto &freed_client = websocket_clients[index];

    const auto new_id = (freed_client.id + 1) % 100; //ID loops every 100
    freed_client = ws_client();
    freed_client.id = new_id;
  }else{
    websocket_clients.emplace(websocket_clients.end()); //otherwise give a new one
    index = websocket_clients.size()-1;
  }
  
  websocket_clients[index].client_idx = client_idx;

  all_websocket_connections.insert(index);
  active_websocket_connections_client_idxs.insert(client_idx);

  return index;
}

template<server_type T>
std::string web_server<T>::get_accept_header_value(std::string input) {
  input += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"; //concatenate magic string
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((const unsigned char*)input.c_str(), input.size(), hash);
  //for input of length 20, base64 output length is 28 (a bit over *4/3 - *4/3 for larger lengths)
  std::string base64data;
  base64data.resize(28); //no need for +1, std::string does that for you
  EVP_EncodeBlock((unsigned char*)base64data.c_str(), hash, SHA_DIGEST_LENGTH);
  return base64data;
}

template<server_type T>
ulong web_server<T>::get_ws_frame_length(const char *buffer){
  ulong packet_length = buffer[1] & 0x7f;
  if(packet_length == 126){
    packet_length = ntohs(*((u_short*)&((uchar*)buffer)[2])) + 2; //the 2 bytes extra needed to store the length are added
  }else if(packet_length == 127){
    packet_length = be64toh(*((u_long*)&((uchar*)buffer)[2])) + 8; //the 8 bytes extra needed to store the length are added, be64toh used because ntohl is 32 bit
  }
  return packet_length + 6; // +6 bytes for the header data
}

template<server_type T>
std::vector<char> web_server<T>::make_ws_frame(const std::string &packet_msg, websocket_non_control_opcodes opcode){
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
  if(msg_size < 126)
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
bool web_server<T>::close_ws_connection_req(int ws_client_idx, bool client_already_closed){
  auto &client_data = websocket_clients[ws_client_idx];
  client_data.currently_writing++;
  active_websocket_connections_client_idxs.erase(client_data.client_idx);
  client_data.websocket_frames = {};
  if(!client_already_closed) {
    auto data = make_ws_frame("", websocket_non_control_opcodes::close_connection);
    tcp_server->write_connection(client_data.client_idx, std::move(data), ws_client_idx);
  }
  return true;
}

template<server_type T>
bool web_server<T>::close_ws_connection_potential_confirm(int ws_client_idx){
  auto &client_data = websocket_clients[ws_client_idx];
  if(client_data.currently_writing == 1){
    if(client_data.close){
      tcp_server->close_connection(client_data.client_idx);
      all_websocket_connections.erase(ws_client_idx); //connection definitely closed by now
      freed_indexes.insert(ws_client_idx);
    }
  }else{
    client_data.currently_writing--;
  }
  return true;
}

template<server_type T>
std::pair<int, std::vector<uchar>> web_server<T>::decode_websocket_frame(std::vector<uchar> data){
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
std::pair<int, std::vector<std::vector<uchar>>> web_server<T>::get_ws_frames(char *buffer, int length, int ws_client_idx){
  std::vector<std::vector<uchar>> frames;

  int remaining_length = length;

  auto &client_data = websocket_clients[ws_client_idx];

  if(client_data.receiving_data.buffer.size() > 0){ //if there is already pending data
    auto *pending_item = &client_data.receiving_data;

    if(pending_item->length == -1){ //assuming the received buffer and the pending buffer are at least 10 bytes long
      if(pending_item->buffer.size() + length < 10){
        close_ws_connection_req(ws_client_idx); //doesn't deal with such small reads - just close the connection
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
      client_data.receiving_data = {};
    
    }else if(required_length == length){ //just enough data
      remaining_length = 0; //we've used up all of the data
      pending_item->buffer.insert(pending_item->buffer.end(), buffer, buffer + length);
      frames.push_back(std::move(pending_item->buffer));
      client_data.receiving_data = {};

    }else{ //too little data
      remaining_length = 0; //we've used up all of the data
      pending_item->buffer.insert(pending_item->buffer.end(), buffer, buffer + length);
    
    }

  }
  
  //by this point, the data in the buffer is guaranteed to start with a new frame
  while(remaining_length > 5){ //you only loop while greater than 5 bytes, since under 6 bytes may not have enough data to get the length
    auto packet_length = get_ws_frame_length(buffer);
    
    if(remaining_length >= packet_length){
      frames.emplace(frames.end(), buffer, buffer + packet_length);
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
    auto *pending_item = &client_data.receiving_data;
    pending_item->buffer = std::vector<uchar>(buffer, buffer + remaining_length);
    if(remaining_length > 10){
      pending_item->length = get_ws_frame_length(buffer);
    }else{
      pending_item->length = -1;
    }
  }

  return {1, frames};
}