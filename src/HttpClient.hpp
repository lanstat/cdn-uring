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

   int HandleFetchData(struct Request *request, bool ipv4) override;
   int HandleReadData(struct Request *request, int response) override;

  private:
   void AddReadRequest(struct Request *request, int fd, int cache_socket);

  protected:
   void ReleaseSocket(struct Request *inner_request) override;
};
#endif
