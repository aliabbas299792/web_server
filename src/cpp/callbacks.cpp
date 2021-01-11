#include "../header/callbacks.h"

#include <openssl/sha.h>
#include <openssl/evp.h>

void a_cb(int client_fd, server *tcp_server, void *custom_obj){ //the accept callback
  // std::cout << "Accepted new connection: " << client_fd << "\n";
}

char *get_accept_header_value(std::string input) {
  input += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"; //concatenate magic string
  
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((const unsigned char*)input.c_str(), input.size(), hash); //calculate SHA1 hash

  const auto str_len = 4*((strlen((const char*)hash)+2)/3); //the +2 saves having to use the ceil function
  auto base64data = (char*)calloc(str_len+1, 1); //+1 for the terminating null that EVP_EncodeBlock adds on
  const int converted_data_length = EVP_EncodeBlock((unsigned char*)base64data, hash, strlen((const char*)hash));
  
  if(converted_data_length != str_len) fatal_error("Base64 encode failed");

  return base64data;
}

void r_cb(int client_fd, char *buffer, unsigned int length, server *tcp_server, void *custom_obj){
  std::vector<std::string> headers;

  bool accept_bytes = false;
  std::string sec_websocket_key = "";

  const auto websocket_key_token = "Sec-WebSocket-Key: ";

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
    const auto basic_web_server = (web_server*)custom_obj;

    std::cout << "Test: " << get_accept_header_value("dGhlIHNhbXBsZSBub25jZQ==") << "\n";

    const std::string accept_header_value = get_accept_header_value(sec_websocket_key);
    const auto resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept_header_value + "\r\n\r\n";

    std::vector<char> data(resp.size());
    std::memcpy(data.data(), resp.c_str(), resp.size());

    //tcp_server->add_write_req(client_fd, data, resp.size());
    tcp_server->write_socket(client_fd, std::move(data));
  } else if(!strcmp(strtok_r((char*)headers[0].c_str(), " ", &saveptr), "GET")){ //get callback
    char *path = strtok_r(nullptr, " ", &saveptr);
    std::string processed_path = std::string(&path[1], strlen(path)-1);
    processed_path = processed_path == "" ? "public/index.html" : "public/"+processed_path;

    char *http_version = strtok_r(nullptr, " ", &saveptr);

    std::vector<char> send_data;
    
    if((send_data = ((web_server*)custom_obj)->read_file_web(processed_path, 200, accept_bytes)).size() != 0){
      tcp_server->write_socket(client_fd, std::move(send_data));
    }else{
      send_data = ((web_server*)custom_obj)->read_file_web("public/404.html", 400);
      tcp_server->write_socket(client_fd, std::move(send_data));
    }
  } else { //if nothing else, then just add in another read request for this socket, since we're not writing
    //tcp_server->add_read_req(client_fd);
  }
}

void w_cb(int client_fd, server *tcp_server, void *custom_obj){
  // std::cout << "write_socket callback\n";
  tcp_server->close_socket(client_fd); //for web requests you close the socket right after
  //tcp_server->add_read_req(client_fd);
}