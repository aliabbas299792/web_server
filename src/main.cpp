#include "header/callbacks.h"

void sigint_handler(int sig_number){
  std::cout << " Shutting down...\n";
  exit(0);
}

void sigpipe_handler(int sig_number){}

int main(){
  signal(SIGINT, sigint_handler); //signal handler for when Ctrl+C is pressed
  signal(SIGPIPE, sigpipe_handler); //signal handler for when a connection is closed while writing

  web_server basic_web_server;
  server tcp_server(a_cb, r_cb, w_cb, &basic_web_server); //pass function pointers and a custom object

  return 0;
}