#include "../header/callbacks.h"
#include "../header/utility.h"

#include <openssl/sha.h>
#include <openssl/evp.h>

template<server_type T>
void a_cb(int client_idx, server<T> *tcp_server, void *custom_obj){ //the accept callback

}

template<server_type T>
void r_cb(int client_idx, char *buffer, unsigned int length, ulong custom_info, server<T> *tcp_or_tls_server, void *custom_obj){
  const auto basic_web_server = (web_server<T>*)custom_obj;

  const auto ws_client_idx = (int32_t)custom_info;

  if(basic_web_server->is_valid_http_req(buffer, length)){ //if not a valid HTTP req, then probably a websocket frame
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
      basic_web_server->websocket_accept_read_cb(sec_websocket_key, client_idx, tcp_or_tls_server); //this will accept websockets
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
  } else if(basic_web_server->all_websocket_connections.count(ws_client_idx)) { //this bit should be just websocket frames
    basic_web_server->websocket_process_read_cb(ws_client_idx, buffer, length); //this is the main websocket callback, deals with receiving messages, and sending them too if it needs/wants to
  }
}

template<server_type T>
void w_cb(int client_idx, ulong custom_info, server<T> *tcp_or_tls_server, void *custom_obj){
  const auto basic_web_server = (web_server<T>*)custom_obj;
  const auto ws_client_idx = (int32_t)custom_info;

  if(!basic_web_server->websocket_process_write_cb(ws_client_idx)){ 
    //if this is a websocket that is in the process of closing, it will let it close and then exit the function, otherwise we read from the function
    tcp_or_tls_server->read_connection(client_idx, ws_client_idx);
  }else{
    tcp_or_tls_server->close_connection(client_idx); //for web requests you close the connection right after
  }
}