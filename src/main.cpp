#include "header/callbacks.h"

void sigint_handler(int sig_number){
  std::cout << " Shutting down...\n";
  exit(0);
}

void sigpipe_handler(int sig_number){}

#define CERT_FILE "fullchain.cer"
#define KEY_FILE  "website.key"

WOLFSSL_CTX *ctx;

int main(){
  signal(SIGINT, sigint_handler); //signal handler for when Ctrl+C is pressed
  signal(SIGPIPE, sigpipe_handler); //signal handler for when a connection is closed while writing

  //initialise wolfSSL
  wolfSSL_Init();

  //create the wolfSSL context
  if ((ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method())) == NULL)
    fatal_error("Failed to create the WOLFSSL_CTX");

  //load the server certificate
  if (wolfSSL_CTX_use_certificate_file(ctx, CERT_FILE, SSL_FILETYPE_PEM) != SSL_SUCCESS)
    fatal_error("Failed to load the certificate files");

  //load the server's private key
  if (wolfSSL_CTX_use_PrivateKey_file(ctx, KEY_FILE, SSL_FILETYPE_PEM) != SSL_SUCCESS)
    fatal_error("Failed to load the private key file");
  
  //set the wolfSSL callbacks
  wolfSSL_CTX_SetIORecv(ctx, callback_recv);
  wolfSSL_CTX_SetIOSend(ctx, callback_send);

  //create the server objects
  web_server basic_web_server;
  server tcp_server(a_cb, r_cb, w_cb, &basic_web_server); //pass function pointers and a custom object

  return 0;
}