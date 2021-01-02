/* server-tls-callback.c
 *
 * Copyright (C) 2006-2020 wolfSSL Inc.
 *
 * This file is part of wolfSSL. (formerly known as CyaSSL)
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

// modified to use io_uring

/* the usual suspects */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* socket includes */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <liburing.h>
#include <iostream>

/* wolfSSL */
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

#define DEFAULT_PORT 443

#define CERT_FILE "fullchain.cer"
#define KEY_FILE  "website.key"

struct rw_cb_context {
  rw_cb_context(io_uring *ring, int sockfd) : ring(ring), sockfd(sockfd) {}
  io_uring *ring = nullptr;
  int sockfd = -1;
};

int callback_recv(WOLFSSL* ssl, char* buff, int sz, void* ctx){
  int sockfd = ((rw_cb_context*)ctx)->sockfd;
  io_uring *ring = ((rw_cb_context*)ctx)->ring;

  io_uring_sqe *sqe = io_uring_get_sqe(ring); //get a valid SQE (correct index and all)
  io_uring_prep_read(sqe, sockfd, buff, sz, 0); //don't read at an offset
  io_uring_submit(ring); //submits the event

  io_uring_cqe *cqe;
  char ret = io_uring_wait_cqe(ring, &cqe);
  io_uring_cqe_seen(ring, cqe); //mark this CQE as seen

  return cqe->res;
}

int callback_send(WOLFSSL* ssl, char* buff, int sz, void* ctx){
  int sockfd = ((rw_cb_context*)ctx)->sockfd;
  io_uring *ring = ((rw_cb_context*)ctx)->ring;

  io_uring_sqe *sqe = io_uring_get_sqe(ring); //get a valid SQE (correct index and all)
  io_uring_prep_write(sqe, sockfd, buff, sz, 0); //don't read at an offset
  io_uring_submit(ring); //submits the event

  io_uring_cqe *cqe;
  char ret = io_uring_wait_cqe(ring, &cqe);
  io_uring_cqe_seen(ring, cqe); //mark this CQE as seen

  return cqe->res;
}

int main() {
  int sockfd, connd;
  sockaddr_in servAddr, clientAddr;
  socklen_t size = sizeof(clientAddr);
  char buff[256];
  size_t len;
  int shutdown = 0, ret;
  const char* reply = "I hear ya fa shizzle!\n";

  /* declare wolfSSL objects */
  WOLFSSL_CTX* ctx;
  WOLFSSL*     ssl;

  /* Initialise io_uring objects */
  io_uring ring;
  io_uring_queue_init(256, &ring, 0);

  /* Initialize wolfSSL */
  wolfSSL_Init();
  
  /* Create a socket that uses an internet IPv4 address,
    * Sets the socket to be stream based (TCP),
    * 0 means choose the default protocol. */
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    fprintf(stderr, "ERROR: failed to create the socket\n");
    return -1;
  }

  /* Create and initialize WOLFSSL_CTX */
  if ((ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method())) == NULL) {
    fprintf(stderr, "ERROR: failed to create WOLFSSL_CTX\n");
    return -1;
  }

  /* Load server certificates into WOLFSSL_CTX */
  if (wolfSSL_CTX_use_certificate_file(ctx, CERT_FILE, SSL_FILETYPE_PEM) != SSL_SUCCESS) {
    fprintf(stderr, "ERROR: failed to load %s, please check the file.\n", CERT_FILE);
    return -1;
  }

  /* Load server key into WOLFSSL_CTX */
  if (wolfSSL_CTX_use_PrivateKey_file(ctx, KEY_FILE, SSL_FILETYPE_PEM) != SSL_SUCCESS) {
    fprintf(stderr, "ERROR: failed to load %s, please check the file.\n", KEY_FILE);
    return -1;
  }

  /* Register callbacks */
  wolfSSL_SetIORecv(ctx, callback_recv);
  wolfSSL_SetIOSend(ctx, callback_send);

  /* Initialize the server address struct with zeros */
  memset(&servAddr, 0, sizeof(servAddr));

  /* Fill in the server address */
  servAddr.sin_family      = AF_INET;             /* using IPv4      */
  servAddr.sin_port        = htons(DEFAULT_PORT); /* on DEFAULT_PORT */
  servAddr.sin_addr.s_addr = INADDR_ANY;          /* from anywhere   */

  /* Bind the server socket to our port */
  if (bind(sockfd, (struct sockaddr*)&servAddr, sizeof(servAddr)) == -1) {
    fprintf(stderr, "ERROR: failed to bind\n");
    return -1;
  }

  /* Listen for a new connection, allow 5 pending connections */
  if (listen(sockfd, 5) == -1) {
    fprintf(stderr, "ERROR: failed to listen\n");
    return -1;
  }

  /* Continue to accept clients until shutdown is issued */
  while (!shutdown) {
    printf("Waiting for a connection...\n");

    /* Accept client connections */
    if ((connd = accept(sockfd, (struct sockaddr*)&clientAddr, &size)) == -1) {
      fprintf(stderr, "ERROR: failed to accept the connection\n\n");
      return -1;
    }

    /* Create a WOLFSSL object */
    if ((ssl = wolfSSL_new(ctx)) == NULL) {
      fprintf(stderr, "ERROR: failed to create WOLFSSL object\n");
      return -1;
    }

    /* Attach wolfSSL to the socket */
    wolfSSL_set_fd(ssl, connd);
    
    //sets the read/write contexts
    rw_cb_context ctx_data(&ring, connd);
    wolfSSL_SetIOReadCtx(ssl, &ctx_data);
    wolfSSL_SetIOWriteCtx(ssl, &ctx_data);

    /* Establish TLS connection */
    ret = wolfSSL_accept(ssl);
    if (ret != SSL_SUCCESS) {
      fprintf(stderr, "wolfSSL_accept error = %d\n", wolfSSL_get_error(ssl, ret));
      return -1;
    }

    printf("Client connected successfully\n");

    /* Read the client data into our buff array */
    memset(buff, 0, sizeof(buff));
    if (wolfSSL_read(ssl, buff, sizeof(buff)-1) == -1) {
      fprintf(stderr, "ERROR: failed to read\n");
      return -1;
    }

    /* Print to stdout any data the client sends */
    printf("Client: %s\n", buff);

    /* Check for server shutdown command */
    if (strncmp(buff, "shutdown", 8) == 0) {
      printf("Shutdown command issued!\n");
      shutdown = 1;
    }

    /* Write our reply into buff */
    memset(buff, 0, sizeof(buff));
    memcpy(buff, reply, strlen(reply));
    len = strnlen(buff, sizeof(buff));

    /* Reply back to the client */
    if (wolfSSL_write(ssl, buff, len) != len) {
      fprintf(stderr, "ERROR: failed to write\n");
      return -1;
    }
    
    /* Cleanup after this connection */
    wolfSSL_free(ssl);      /* Free the wolfSSL object              */
    close(connd);           /* Close the connection to the client   */
  }

  printf("Shutdown complete\n");



  /* Cleanup and return */
  wolfSSL_CTX_free(ctx);  /* Free the wolfSSL context object          */
  wolfSSL_Cleanup();      /* Cleanup the wolfSSL environment          */
  close(sockfd);          /* Close the socket listening for clients   */
  return 0;               /* Return reporting a success               */
}