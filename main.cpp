#include "server.h"

void sigint_handler(int sig_number){
  std::cout << " Shutting down...\n";
  exit(0);
}
const char *testpage = \
        "HTTP/1.0 200 OK\r\n"
        "Content-type: text/html\r\n"
        "\r\n"
        "<html>"
        "<head>"
        "<title>Ali's website</title>"
        "</head>"
        "<body>"
        "<h1>Hello</h1>"
        "<p>The test page works.</p>"
        "</body>"
        "</html>";

void accept_callback(int client_fd, server *web_server){
  //std::cout << "accepted client with client_fd: " << client_fd << "\n";
}

void read_callback(int client_fd, int iovec_count, iovec iovecs[], server *web_server){
  std::vector<std::string> headers;
  
  for(int i = 0; i < iovec_count; i++){
    char *str = nullptr;
    while((str = strtok(((char*)iovecs[i].iov_base), "\r\n"))){ //retrieves the headers
      headers.push_back(std::string(str, strlen(str)));
      iovecs[i].iov_base = nullptr;
    }
  }

  char *data = nullptr;
  if(strcmp(strtok((char*)headers[0].c_str(), " "), "GET")){
    char *directory = strtok(nullptr, " ");
    char *http_version = strtok(nullptr, " ");
  }

  auto content_length = strlen(testpage);
  auto write_data = malloc(content_length);
  std::memcpy(write_data, testpage, content_length);
  web_server->add_write_req(client_fd, write_data, content_length); //pass the data to the write function
  close(client_fd);
}

void write_callback(int client_fd, server *web_server){

}

int main(){
  signal(SIGINT, sigint_handler); //signal handler for when Ctrl+C is pressed

  server web_server(accept_callback, read_callback, write_callback);

  return 0;
}