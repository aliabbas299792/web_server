// #include "../header/web_server.h"

// std::string get_accept_header_value(std::string input) {
//   input += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"; //concatenate magic string
//   unsigned char hash[SHA_DIGEST_LENGTH];
//   SHA1((const unsigned char*)input.c_str(), input.size(), hash);
//   //for input of length 20, base64 output length is 28 (a bit over *4/3 - *4/3 for larger lengths)
//   auto base64data = (char*)calloc(28+1, 1); //+1 for the terminating null that EVP_EncodeBlock adds on
//   EVP_EncodeBlock((unsigned char*)base64data, hash, SHA_DIGEST_LENGTH);
//   return base64data;
// }

// ulong web_server::get_ws_frame_length(const char *buffer){
//   ulong packet_length = buffer[1] & 0x7f;
//   if(packet_length == 126){
//     packet_length = ntohs(*((u_short*)&((uchar*)buffer)[2])) + 2; //the 2 bytes extra needed to store the length are added
//   }else if(packet_length == 127){
//     packet_length = be64toh(*((u_long*)&((uchar*)buffer)[2])) + 8; //the 8 bytes extra needed to store the length are added, be64toh used because ntohl is 32 bit
//   }
//   return packet_length + 6; // +6 bytes for the header data
// }

// std::vector<char> web_server::make_ws_frame(std::string packet_msg, websocket_non_control_opcodes opcode){
//   //gets the correct offsets and sizes
//   int offset = 2; //first 2 bytes for the header data (excluding the extended length bit)
//   uchar payload_len_char = 0;
//   ushort payload_len_short = 0;
//   ulong payload_len_long = 0;
//   const auto msg_size = packet_msg.size();
//   if(msg_size < 126){ //less than 126 bytes long
//     payload_len_char = msg_size;
//   }else if(msg_size < 65536){ //less than 2^16 bytes long
//     offset += 2;
//     payload_len_short = htons(msg_size);
//   }else{ //more than 2^16 bytes long
//     offset += 8;
//     payload_len_long = htobe64(msg_size);
//   }
  
//   std::vector<char> data(offset + msg_size);
//   data[0] = 128 | opcode; //not gonna do fragmentation, so set the fin bit, and the opcode
//   //don't mask frames being sent to the client

//   //sets the correct payload length
//   if(payload_len_char)
//     data[1] = payload_len_char;
//   else if(payload_len_short){
//     data[1] = 126;
//     *((ushort*)&data[2]) = payload_len_short;
//   }else{
//     data[1] = 127;
//     *((ulong*)&data[2]) = payload_len_long;
//   }
  
//   std::memcpy(&data[offset], packet_msg.c_str(), msg_size);

//   return data;
// }

// template<server_type T>
// bool web_server::close_ws_connection_req(int client_idx, bool client_already_closed = false){
//   this->close_pending_ops_map[client_idx]++;
//   this->websocket_connections.erase(client_idx);
//   this->websocket_frames.erase(client_idx);
//   if(!client_already_closed) {
//     auto data = make_ws_frame("", websocket_non_control_opcodes::close_connection);
//     tcp_server->write_connection(client_idx, std::move(data));
//   }
//   return true;
// }

// template<server_type T>
// bool web_server::close_ws_connection_confirm(int client_idx){
//   if(!(this->close_pending_ops_map[client_idx] - 1)){
//     this->close_pending_ops_map.erase(client_idx);
//     tcp_server->close_connection(client_idx);
//   }else{
//     this->close_pending_ops_map[client_idx]--;
//   }
//   return true;
// }

// std::pair<int, std::vector<uchar>> web_server::decode_websocket_frame(std::vector<uchar> data){
//   const uint fin = (data[0] & 0x80) == 0x80;
//   const uint opcode = data[0] & 0xf;
//   const uint mask = (data[1] & 0x80) == 0x80;

//   if(!mask) return {-1, {}}; //mask must be set
//   if(opcode == 0x8) return {-3, {}}; //the close opcode
//   if(opcode == 0xA) return {3, {}}; //the pong opcode

//   int offset = 0;

//   ulong length = data[1] & 0x7f;
//   if(length == 126){
//     offset = 2;
//     length = ntohs(*((u_short*)&data[2]));
//   }else if(length == 127){
//     offset = 8;
//     length = ntohs(*((u_long*)&data[2]));
//   }

//   const std::vector<uchar> masking_key{ data[2+offset], data[3+offset], data[4+offset], data[5+offset] };

//   std::vector<uchar> decoded{};
//   for(int i = 6+offset; i < data.size(); i++){
//     decoded.push_back(data[i] ^ masking_key[(i - (6 + offset)) % 4]);
//   }

//   if(opcode == 0x9) return {2, decoded}; //the ping opcode
//   if(!fin) return {-2, decoded}; //fin bit not set, so put this in a pending larger buffer of decoded data
  
//   return {1, decoded}; //succesfully decoded, and is the final frame
// }

// template<server_type T>
// std::pair<int, std::vector<std::vector<uchar>>> web_server::get_ws_frames(char *buffer, int length, int client_idx){
//   std::vector<std::vector<uchar>> frames;

//   int remaining_length = length;

//   if(this->receiving_data.count(client_idx)){ //if there is already pending data
//     auto *pending_item = &this->receiving_data[client_idx];

//     if(pending_item->length == -1){ //assuming the received buffer and the pending buffer are at least 10 bytes long
//       if(pending_item->buffer.size() + length < 10){
//         // std::cout << "super small read...\n";
//         close_ws_connection_req(client_idx, tcp_server, this); //doesn't deal with such small reads - just close the connection
//         return {-1, {}};
//       }else{
//         std::vector<char> temp_buffer(10); //we only need first 10 bytes for length
//         temp_buffer.insert(temp_buffer.begin(), pending_item->buffer.begin(), pending_item->buffer.end());
//         temp_buffer.insert(temp_buffer.begin(), buffer, buffer + ( 10 - pending_item->buffer.size() ));
//         pending_item->length = get_ws_frame_length(&temp_buffer[0]);
//       }
//     }
    
//     const auto required_length = pending_item->length - pending_item->buffer.size();

//     if(required_length < length){ //more than enough data
//       remaining_length = length - required_length;
//       pending_item->buffer.insert(pending_item->buffer.end(), buffer, buffer + required_length);
//       frames.push_back(std::move(pending_item->buffer));

//       char *remaining_data = nullptr;
//       remove_first_n_elements(buffer, (int)length, remaining_data, (int)required_length);
//       buffer = remaining_data;
//       this->receiving_data.erase(client_idx);
    
//     }else if(required_length == length){ //just enough data
//       remaining_length = 0; //we've used up all of the data
//       pending_item->buffer.insert(pending_item->buffer.end(), buffer, buffer + length);
//       frames.push_back(std::move(pending_item->buffer));
//       this->receiving_data.erase(client_idx);

//     }else{ //too little data
//       remaining_length = 0; //we've used up all of the data
//       pending_item->buffer.insert(pending_item->buffer.end(), buffer, buffer + length);
    
//     }

//   }
  
//   //by this point, the data in the buffer is guaranteed to start with a new frame
//   while(remaining_length > 10){ //you only loop while greater than 10 bytes, since under 10 bytes may not have enough data to get the length
//     auto packet_length = get_ws_frame_length(buffer);
    
//     if(remaining_length >= packet_length){
//       frames.push_back(std::vector<uchar>(buffer, buffer + packet_length));
//       char *remaining_data = nullptr;
//       remove_first_n_elements(buffer, (int)remaining_length, remaining_data, (int)packet_length);
//       buffer = remaining_data;
//       remaining_length -= packet_length;
//     }else{
//       break;
//     }
//   }

//   //by this point only the beginnings of frames should be left, if the remaining_length is not 0
//   if(remaining_length > 0){
//     auto *pending_item = &this->receiving_data[client_idx];
//     pending_item->buffer = std::vector<uchar>(buffer, buffer + remaining_length);
//     if(remaining_length > 10){
//       pending_item->length = get_ws_frame_length(buffer);
//     }else{
//       pending_item->length = -1;
//     }
//   }

//   return {1, frames};
// }

// template<server_type T>
// void web_server::force_shut_ws_connection(int client_idx){
//   this->close_pending_ops_map.erase(client_idx);
//   this->websocket_connections.erase(client_idx);
//   this->websocket_frames.erase(client_idx);
//   tcp_server->close_connection(client_idx);
// }