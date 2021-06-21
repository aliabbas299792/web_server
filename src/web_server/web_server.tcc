#pragma once
#include "../header/web_server.h"
#include "../header/utility.h"

template<server_type T>
web_server<T>::web_server(){
  io_uring_queue_init(QUEUE_DEPTH, &ring, 0); //no flags, setup the queue
}

template<server_type T>
bool web_server<T>::get_process(std::string &path, bool accept_bytes, const std::string& sec_websocket_key, int client_idx){
  char *saveptr = nullptr;
  const char* token = strtok_r((char*)path.c_str(), "/", &saveptr);
  const char* subdir = token ? token : "";

  if( (strlen(subdir) == 2 && strncmp(subdir, "ws", 2) == 0)  && sec_websocket_key != ""){
    websocket_accept_read_cb(sec_websocket_key, path.substr(2), client_idx);
    return true;
  }else{
    path = path == "" ? "public/index.html" : "public/"+path;
    
    if(send_file_request(client_idx, path, accept_bytes, 200))
      return true;
    return false;
  }
}

template<server_type T>
bool web_server<T>::is_valid_http_req(const char* buff, int length){
  if(length < 16) return false; //minimum size for valid HTTP request is 16 bytes
  const char *types[] = { "GET ", "POST ", "PUT ", "DELETE ", "PATCH " };
  u_char valid = 0x1f;
  for(int i = 0; i < 7; i++) //length of "DELETE " is 7 characters
    for(int j = 0; j < 5; j++) //5 different types
      if(i < strlen(types[j]) && (valid >> j) & 0x1 && types[j][i] != buff[i]) valid &= 0x1f ^ (1 << j);
  return valid;
}

template<server_type T>
std::string web_server<T>::get_content_type(std::string filepath){
  char *file_extension_data = (char*)filepath.c_str();
  std::string file_extension = "";
  char *saveptr = nullptr;
  while((file_extension_data = strtok_r(file_extension_data, ".", &saveptr))){
    file_extension = file_extension_data;
    file_extension_data = nullptr;
  }

  if(file_extension == "html" || file_extension == "htm")
    return "Content-Type: text/html\r\n";
  if(file_extension == "css")
    return "Content-Type: text/css\r\n";
  if(file_extension == "js")
    return "Content-Type: text/javascript\r\n";
  if(file_extension == "opus")
    return "Content-Type: audio/opus\r\n";
  if(file_extension == "mp3")
    return "Content-Type: audio/mpeg\r\n";
  if(file_extension == "mp4")
    return "Content-Type: video/mp4\r\n";
  if(file_extension == "gif")
    return "Content-Type: image/gif\r\n";
  if(file_extension == "png")
    return "Content-Type: image/png\r\n";
  if(file_extension == "jpg" || file_extension == "jpeg")
    return "Content-Type: image/jpeg\r\n";
  if(file_extension == "txt")
    return "Content-Type: text/plain\r\n";
  return "Content-Type: application/octet-stream\r\n";
}

template<server_type T>
bool web_server<T>::send_file_request(int client_idx, const std::string &filepath, bool accept_bytes, int response_code){
  const auto file_fd = open(filepath.c_str(), O_RDONLY);

  std::string header_first_line{};
  switch(response_code){
    case 200:
      header_first_line = "HTTP/1.1 200 OK\r\n";
      break;
    default:
      header_first_line = "HTTP/1.1 404 Not Found\r\n";
  }

  if(file_fd < 0)
    return false;

  const auto file_size = get_file_size(file_fd);

  const auto content_length = std::to_string(file_size);
  const auto content_type = get_content_type(filepath);

  std::string headers = "";
  if(accept_bytes){
    headers = header_first_line;
    headers += content_type;
    headers += "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nContent-Length: ";
    headers += content_length;
    headers += "\r\nRange: bytes=0-";
    headers += content_length;
    headers += "/";
  }else{
    headers = header_first_line;
    headers += content_type;
    headers += "Content-Length: ";
  }
  headers += content_length;
  headers += "\r\nConnection:Keep-Alive\r\n\r\n";

  std::vector<char> send_buffer(file_size + headers.size());

  std::memcpy(&send_buffer[0], headers.c_str(), headers.size());
  
  tcp_server->custom_read_req(file_fd, file_size, client_idx, std::move(send_buffer), headers.size());

  return true;
}

template<server_type T>
void web_server<T>::set_tcp_server(server<T> *tcp_server){
  this->tcp_server = tcp_server;
}