#ifndef SRC_HTTPCLIENT_HPP_
#define SRC_HTTPCLIENT_HPP_

#include <liburing.h>

#include <iostream>
#include <string>
#include <unordered_map>

#include "Cache.hpp"
#include "Request.hpp"
#include "Server.hpp"
#include "Http.hpp"

class HttpClient: public Http {
  public:
   HttpClient();

   int HandleFetchData(struct Request *request) override;
   int HandleReadData(struct Request *request) override;
};
#endif
