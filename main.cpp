#include "server.h"

void sigint_handler(int sig_number){
  std::cout << " Shutting down...\n";
  exit(0);
}

void accept_callback(int client_fd, server *web_server){
  //std::cout << "accepted client with client_fd: " << client_fd << "\n";
}

void read_callback(int client_fd, int iovec_count, iovec iovecs[], server *web_server){
  
}

void write_callback(int client_fd, server *web_server){

}

int main(){
  signal(SIGINT, sigint_handler); //signal handler for when Ctrl+C is pressed

  server web_server(accept_callback, read_callback, write_callback);

  return 0;
}