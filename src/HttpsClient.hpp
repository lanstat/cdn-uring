#ifndef SRC_HTTPSCLIENT_HPP_
#define SRC_HTTPSCLIENT_HPP_

#include <liburing.h>
#include <openssl/ssl.h>

#include <iostream>
#include <string>
#include <unordered_map>

#include "Cache.hpp"
#include "Http.hpp"
#include "Request.hpp"
#include "Server.hpp"

class HttpsClient : public Http {
 public:
  HttpsClient();

  int HandleFetchData(struct Request *request, bool ipv4) override;
  int HandleReadData(struct Request *request, int response) override;

 private:
  void AddReadRequest(struct Request *request, SSL *ssl, SSL_CTX *context,
                      int fd);
  void CloseSSL(int socket_fd, SSL *ssl, SSL_CTX *context);

 protected:
  void ReleaseSocket(struct Request *request) override;
  bool ProcessError(SSL *ssl, int last_error);
};
#endif
