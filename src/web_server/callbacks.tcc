#include "../header/callbacks.h"
#include "../header/utility.h"
#include "../header/web_server/cache.h"

#include <openssl/sha.h>
#include <openssl/evp.h>

#include <sys/inotify.h>

template<server_type T>
void accept_cb(int client_idx, server<T> *tcp_server, void *custom_obj){ //the accept callback
  const auto basic_web_server = (web_server<T>*)custom_obj;
  basic_web_server->new_tcp_client(client_idx);
}

template<server_type T>
void close_cb(int client_idx, server<T> *tcp_server, void *custom_obj){ //the accept callback
  const auto basic_web_server = (web_server<T>*)custom_obj;
  basic_web_server->kill_tcp_client(client_idx);
}

template<server_type T>
void event_cb(server<T> *tcp_server, void *custom_obj, std::vector<char> &&buff){ //the accept callback
  const auto basic_web_server = (web_server<T>*)custom_obj;
  const auto &client_idxs = basic_web_server->active_websocket_connections_client_idxs;

  std::string str = "hello world....\n";
  
  // auto data = basic_web_server->make_ws_frame(str, websocket_non_control_opcodes::binary_frame); //echos back whatever you send
  // tcp_server->broadcast_message(client_idxs.cbegin(), client_idxs.cend(), client_idxs.size(), std::move(data));
}

template<server_type T>
void custom_read_cb(int client_idx, int fd, std::vector<char> &&buff, server<T> *tcp_server, void *custom_obj){
  const auto basic_web_server = (web_server<T>*)custom_obj;

  if(fd == basic_web_server->web_cache.inotify_fd){
    basic_web_server->web_cache.inotify_event_handler(reinterpret_cast<inotify_event*>(&buff[0])->wd);
    tcp_server->custom_read_req(basic_web_server->web_cache.inotify_fd, sizeof(inotify_event)); //always read from inotify_fd - we only read size of event, since we monitor files
  }else{
    close(fd); //close the file fd finally, since we've read what we needed to

    const auto &filepath = basic_web_server->tcp_clients[client_idx].last_requested_read_filepath;
    basic_web_server->web_cache.try_insert_item(client_idx, filepath, std::move(buff));

    const auto ret_data = basic_web_server->web_cache.fetch_item(filepath, client_idx, basic_web_server->tcp_clients[client_idx]);

    tcp_server->write_connection(client_idx, ret_data.buff, ret_data.size);
  }
}

template<server_type T>
void read_cb(int client_idx, char *buffer, unsigned int length, server<T> *tcp_server, void *custom_obj){
  const auto basic_web_server = (web_server<T>*)custom_obj;
  
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
        !basic_web_server->get_process(path, accept_bytes, sec_websocket_key, client_idx)
      )
    {
      basic_web_server->send_file_request(client_idx, "public/404.html", false, 400); //sends 404 request, should be cached if possible
    }
  } else if(basic_web_server->active_websocket_connections_client_idxs.count(client_idx)) { //this bit should be just websocket frames, and we only want to hear from active websockets, not closing ones
    basic_web_server->websocket_process_read_cb(client_idx, buffer, length); //this is the main websocket callback, deals with receiving messages, and sending them too if it needs/wants to
  }else{
    basic_web_server->close_connection(client_idx);
  }
}

template<server_type T>
void write_cb(int client_idx, server<T> *tcp_server, void *custom_obj){
  const auto basic_web_server = (web_server<T>*)custom_obj;

  if(basic_web_server->websocket_process_write_cb(client_idx)){ 
    //if this is a websocket that is in the process of closing, it will let it close and then exit the function, otherwise we read from the function
    tcp_server->read_connection(client_idx);
  }else{
    basic_web_server->close_connection(client_idx); //for web requests you close the connection right after
  }
}