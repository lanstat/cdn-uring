#ifndef SRC_HTTPCLIENT_HPP_
#define SRC_HTTPCLIENT_HPP_

#include <liburing.h>

#include <iostream>
#include <string>
#include <unordered_map>

#include "Cache.hpp"
#include "Http.hpp"
#include "Request.hpp"
#include "Server.hpp"

class HttpClient : public Http {
 public:
  HttpClient();

  int HandleReadHeaderRequest(struct Request *http, int readed) override;
  bool HandleFetchRequest(struct Request *request, bool ipv4) override;
  int HandleReadData(struct Request *request, int response) override;

 protected:
  void ReleaseSocket(struct Request *http) override;
};
#endif
