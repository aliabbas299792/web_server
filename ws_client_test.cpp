#include <iostream>
#include <chrono>
#include <netdb.h> //for networking stuff like addrinfo
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <algorithm>
#include <string>

#define PORT "443"
#define DOMAIN "radio.erewhon.xyz"

int main(){
  addrinfo hints, *server_info, *traverser;
  int responseCode;
  char serverAddress[INET6_ADDRSTRLEN];
  
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  
  addrinfo *all_server_info = (addrinfo*)std::malloc(sizeof(addrinfo));

  if((responseCode = getaddrinfo(DOMAIN, PORT, &hints, &server_info)) != 0){
    perror("getaddrinfo");
    exit(1);
  }

  for(traverser = server_info; traverser != NULL; traverser = traverser->ai_next){
    if(socket(traverser->ai_family, traverser->ai_socktype, traverser->ai_protocol) == -1){
      perror("socket creation");
      continue;
    }

    std::memcpy(all_server_info, traverser, sizeof(addrinfo));

    break;
  }

  if(traverser == NULL){
    perror("socket failed to connect");
    exit(1);
  }

  int ret = 0;
  WOLFSSL_CTX* ctx;

  /* Initialize wolfSSL */
  if ((ret = wolfSSL_Init()) != WOLFSSL_SUCCESS) {
    fprintf(stderr, "ERROR: Failed to initialize the library\n");
    std::exit(-1);
  }

  /* Create and initialize WOLFSSL_CTX */
  if ((ctx = wolfSSL_CTX_new(wolfTLSv1_3_client_method())) == NULL) {
    fprintf(stderr, "ERROR: failed to create WOLFSSL_CTX\n");
    std::exit(-1);
  }
  
  wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, NULL);

  std::ofstream outfile("ws_test_output.txt", std::ios_base::app);

  std::string out = "First column is the one that matters, memory usage in kB before opening any clients, and after\n\n";
  outfile.write(out.c_str(), out.size());

  std::string pid_str{};
  {
    FILE *fp;
    char path[1035];

    /* Open the command for reading. */
    fp = popen("ps -ef | grep \"[[:digit:]] ./test_binary_server\" | grep -oh \"root[ ]*[0-9]*\" | grep -oh \"[0-9]*\"", "r");
    if (fp == NULL) {
      printf("Failed to run command\n" );
      exit(1);
    }

    /* Read the output a line at a time - output it. */
    while (fgets(path, sizeof(path), fp) != NULL) {
      pid_str = path;
    }

    /* close */
    pclose(fp);
  }

  pid_str.erase(std::remove(pid_str.begin(), pid_str.end(), '\n'), pid_str.end());

  std::string get_mem_cmd = "sudo pmap -x " + pid_str + " | grep total";

  while(true){
    std::vector<WOLFSSL*> to_close_ssl{};
    std::vector<int> to_close_sockets{};

    {
      FILE *fp;
      char path[1035];

      /* Open the command for reading. */
      fp = popen(get_mem_cmd.c_str(), "r");
      if (fp == NULL) {
        printf("Failed to run command\n" );
        exit(1);
      }

      /* Read the output a line at a time - output it. */
      while (fgets(path, sizeof(path), fp) != NULL) {
        std::string out = "Before:     " + std::string(path);
        outfile.write(out.c_str(), out.size());
      }

      /* close */
      pclose(fp);
    }

    outfile.flush();

    for(int i = 0; i < 40; i++){
      int sockfd = socket(all_server_info->ai_family, all_server_info->ai_socktype, all_server_info->ai_protocol);
      int rc = connect(sockfd, all_server_info->ai_addr, all_server_info->ai_addrlen);

      WOLFSSL* ssl = nullptr;

      /* Create a WOLFSSL object */
      if ((ssl = wolfSSL_new(ctx)) == NULL) {
        fprintf(stderr, "ERROR: failed to create WOLFSSL object\n");
        std::exit(-1);
      }

      /* Attach wolfSSL to the socket */
      if ((ret = wolfSSL_set_fd(ssl, sockfd)) != WOLFSSL_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to set the file descriptor\n");
        std::exit(-1);
      }

      /* Connect to wolfSSL on the server side */
      if ((ret = wolfSSL_connect(ssl)) != SSL_SUCCESS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
      }

      std::string req = "GET /chat HTTP/1.1\nUpgrade: websocket\nConnection: Upgrade\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\nSec-WebSocket-Version: 13\r\n";
      const unsigned char buff[] = {129,141,219,206,78,169,251,166,43,197,183,161,110,222,180,188,34,205,245};
      wolfSSL_write(ssl, req.c_str(), req.size());
      wolfSSL_write(ssl, buff, strlen((const char*)buff));

      std::this_thread::sleep_for(std::chrono::milliseconds(1 * rand() % 1000));

      to_close_ssl.push_back(ssl);
      to_close_sockets.push_back(sockfd);
    }

    {
      FILE *fp;
      char path[1035];

      /* Open the command for reading. */
      fp = popen(get_mem_cmd.c_str(), "r");
      if (fp == NULL) {
        printf("Failed to run command\n" );
        exit(1);
      }

      /* Read the output a line at a time - output it. */
      while (fgets(path, sizeof(path), fp) != NULL) {
        std::string out = "After:      " + std::string(path) + "\n";
        outfile.write(out.c_str(), out.size());
      }

      /* close */
      pclose(fp);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    for(const auto &ssl : to_close_ssl){
      wolfSSL_shutdown(ssl);
      wolfSSL_free(ssl);
    }

    for(const auto &sockfd : to_close_sockets){
      close(sockfd);
    }
    outfile.flush();
  }
}