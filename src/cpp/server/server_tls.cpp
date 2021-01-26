#include "../../header/server.h"
#include "../../header/utility.h"

void server<server_type::TLS>::close_socket(int client_idx) {
  auto *client = &clients[client_idx];
  wolfSSL_shutdown(client->ssl);
  wolfSSL_free(client->ssl);

  active_connections.erase(client_idx);
  client->sockfd = 0;
  client->send_data = {};
  client->accept_recv_data = {};
  client->accept_last_written = 0;
  client->ssl = nullptr;

  freed_indexes.push(client_idx);
}

void server<server_type::TLS>::write_socket(int client_idx, std::vector<char> &&buff) {
  auto *client = &clients[client_idx];
  client->send_data.push(write_data(std::move(buff)));
  const auto data_ref = client->send_data.front();
  wolfSSL_write(client->ssl, &data_ref.buff[0], data_ref.buff.size()); //writes the data using wolfSSL
}

server<server_type::TLS>::server(
  int listen_port,
  std::string fullchain_location,
  std::string pkey_location,
  accept_callback<server_type::TLS> a_cb,
  read_callback<server_type::TLS> r_cb,
  write_callback<server_type::TLS> w_cb,
  void *custom_obj
){
  this->accept_cb = a_cb;
  this->read_cb = r_cb;
  this->write_cb = w_cb;
  this->custom_obj = custom_obj;

  //initialise wolfSSL
  wolfSSL_Init();

  //create the wolfSSL context
  if((wolfssl_ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method())) == NULL)
    fatal_error("Failed to create the WOLFSSL_CTX");

  //load the server certificate
  if(wolfSSL_CTX_use_certificate_chain_file(wolfssl_ctx, fullchain_location.c_str()) != SSL_SUCCESS)
    fatal_error("Failed to load the certificate files");

  //load the server's private key
  if(wolfSSL_CTX_use_PrivateKey_file(wolfssl_ctx, pkey_location.c_str(), SSL_FILETYPE_PEM) != SSL_SUCCESS)
    fatal_error("Failed to load the private key file");
  
  //set the wolfSSL callbacks
  wolfSSL_CTX_SetIORecv(wolfssl_ctx, tls_recv);
  wolfSSL_CTX_SetIOSend(wolfssl_ctx, tls_send);

  std::cout << "TLS will be used\n";
  
  std::memset(&ring, 0, sizeof(io_uring));
  io_uring_queue_init(QUEUE_DEPTH, &ring, 0); //no flags, setup the queue
  this->listener_fd = setup_listener(listen_port); //setup the listener socket
}

void server<server_type::TLS>::tls_accept(int client_idx){
  auto *client = &clients[client_idx];

  WOLFSSL *ssl = wolfSSL_new(wolfssl_ctx);
  wolfSSL_set_fd(ssl, client->sockfd);

  wolfSSL_SetIOReadCtx(ssl, this);
  wolfSSL_SetIOWriteCtx(ssl, this);

  client->ssl = ssl; //sets the ssl connection

  wolfSSL_accept(ssl); //initialise the wolfSSL accept procedure
}