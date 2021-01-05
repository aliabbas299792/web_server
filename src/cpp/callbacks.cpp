#include "../header/callbacks.h"

#include <openssl/sha.h>
#include <openssl/evp.h>

WOLFSSL *ssl;

int callback_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx){ //receive callback, sends a special accept read request to io_uring, make it not always do that
  int sockfd = ((rw_cb_context*)ctx)->sockfd;
  auto *tcp_server = ((rw_cb_context*)ctx)->tcp_server;

  if(accept_data.count(sockfd)){ //if an entry exists in the map, use the data in it, otherwise make a request for it
    const auto data = accept_data[sockfd];
    const auto all_received = data.first;
    const auto recvdAmount = data.second;

    if(recvdAmount > sz){
      //if the data needed in this call is less than what we have available,
      //then copy that onto the provided buffer, and return the amount read
      std::memcpy(buff, all_received, sz);
      accept_data[sockfd] = { (char*)std::malloc(recvdAmount - sz), recvdAmount - sz };
      std::memset(accept_data[sockfd].first, 0, recvdAmount - sz);
      std::memcpy(accept_data[sockfd].first, &all_received[sz], recvdAmount - sz);
      free(all_received); //making sure to free the original buffer
      return sz;
    }else{
      std::memcpy(buff, all_received, sz); //since this is exactly how much we need, copy the data into the buffer
      free(all_received); //making sure to free the original buffer
      accept_data.erase(sockfd); //and erasing the item from the map, since we've used all the data it provided
      return recvdAmount; //sz == recvdAmount in this case
    }
  }else{
    tcp_server->add_read_req(sockfd, ssl, true);
    return WOLFSSL_CBIO_ERR_WANT_READ; //if there was no data to be read currently, send a request for more data, and respond with this error
  }
}

int callback_send(WOLFSSL* ssl, char* buff, int sz, void* ctx){ //send callback, sends a special accept write request to io_uring, make it not always do that
  int sockfd = ((rw_cb_context*)ctx)->sockfd;
  auto *tcp_server = ((rw_cb_context*)ctx)->tcp_server;

  tcp_server->add_write_req(sockfd, buff, sz, ssl, true);

  return sz;
}

void a_cb(int client_fd, server *tcp_server, void *custom_obj){ //the accept callback
  ssl = wolfSSL_new(ctx);
  wolfSSL_set_fd(ssl, client_fd);

  //set the read/write context data, from this scope,
  //since once execution leaves this scope the references are invalid and we'll have to set the context data again
  rw_cb_context ctx_data(tcp_server, client_fd);
  wolfSSL_SetIOReadCtx(ssl, &ctx_data);
  wolfSSL_SetIOWriteCtx(ssl, &ctx_data);

  wolfSSL_accept(ssl); //initialise the wolfSSL accept procedure
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

  std::cout << "Message: \n" << std::string(buffer, length) << "\n";

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

    char *data = (char*)std::malloc(resp.size());
    std::memcpy(data, resp.c_str(), resp.size());

    tcp_server->add_write_req(client_fd, data, resp.size());
  } else if(!strcmp(strtok_r((char*)headers[0].c_str(), " ", &saveptr), "GET")){ //get callback
    char *path = strtok_r(nullptr, " ", &saveptr);
    std::string processed_path = std::string(&path[1], strlen(path)-1);
    processed_path = processed_path == "" ? "public/index.html" : "public/"+processed_path;

    char *http_version = strtok_r(nullptr, " ", &saveptr);

    char *send_buffer = nullptr;
    int content_length = 0;
    
    if((content_length = ((web_server*)custom_obj)->read_file_web(processed_path, &send_buffer, 200, accept_bytes)) != -1){
      tcp_server->add_write_req(client_fd, send_buffer, content_length); //pass the data to the write function
    }else{
      content_length = ((web_server*)custom_obj)->read_file_web("public/404.html", &send_buffer, 400);
      tcp_server->add_write_req(client_fd, send_buffer, content_length);
    }
  } else { //if nothing else, then just add in another read request for this socket, since we're not writing
    tcp_server->add_read_req(client_fd);
  }
}

void w_cb(int client_fd, server *tcp_server, void *custom_obj){
  close(client_fd); //for web requests you close the socket right after
  //tcp_server->add_read_req(client_fd);
}