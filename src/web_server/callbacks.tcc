#include "../header/callbacks.h"
#include "../header/utility.h"

#include <openssl/sha.h>
#include <openssl/evp.h>

template<server_type T>
void accept_cb(int client_idx, server<T> *tcp_server, void *custom_obj){ //the accept callback

}

template<server_type T>
void event_cb(server<T> *tcp_server, void *custom_obj){ //the accept callback
  const auto basic_web_server = (web_server<T>*)custom_obj;
  const auto &client_idxs = basic_web_server->active_websocket_connections_client_idxs;

  std::string str = "hello world....\n";
  
  auto data = basic_web_server->make_ws_frame(str, websocket_non_control_opcodes::binary_frame); //echos back whatever you send
  tcp_server->broadcast_message(client_idxs.cbegin(), client_idxs.cend(), client_idxs.size(), std::move(data));
}

template<server_type T>
void read_cb(int client_idx, char *buffer, unsigned int length, ulong custom_info, server<T> *tcp_server, void *custom_obj){
  const auto basic_web_server = (web_server<T>*)custom_obj;

  const auto ws_client_idx = (int32_t)custom_info; //in the case this is a websocket frame read

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
      
      if(tempStr.find("Range: bytes=") != std::string::npos)
        accept_bytes = true;
      if(tempStr.find("Sec-WebSocket-Key") != std::string::npos)
        sec_websocket_key = tempStr.substr(strlen(websocket_key_token));
      buffer_str = nullptr;
      headers.push_back(tempStr);
    }

    bool is_GET = !strcmp(strtok_r((char*)headers[0].c_str(), " ", &saveptr), "GET");
    std::string path = &strtok_r(nullptr, " ", &saveptr)[1]; //if it's a valid request it should be a path

    //get callback, if unsuccesful then 404
    if( !is_GET ||
        !basic_web_server->get_process(path, accept_bytes, sec_websocket_key, client_idx, tcp_server)
      )
    {
      auto send_buffer = basic_web_server->read_file_web("public/404.html", 400);
      tcp_server->write_connection(client_idx, std::move(send_buffer));
    }
  } else if(basic_web_server->all_websocket_connections.count(ws_client_idx)) { //this bit should be just websocket frames
    basic_web_server->websocket_process_read_cb(ws_client_idx, buffer, length); //this is the main websocket callback, deals with receiving messages, and sending them too if it needs/wants to
  }else{
    tcp_server->close_connection(client_idx);
  }
}

template<server_type T>
void write_cb(int client_idx, ulong custom_info, server<T> *tcp_server, void *custom_obj){
  const auto basic_web_server = (web_server<T>*)custom_obj;
  const auto ws_client_idx = (int32_t)custom_info;

  if(basic_web_server->websocket_process_write_cb(ws_client_idx)){ 
    //if this is a websocket that is in the process of closing, it will let it close and then exit the function, otherwise we read from the function
    tcp_server->read_connection(client_idx, ws_client_idx);
  }else{
    tcp_server->close_connection(client_idx); //for web requests you close the connection right after
  }
}