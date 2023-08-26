#ifndef SRC_HELPERS_HPP_
#define SRC_HELPERS_HPP_

#include <liburing.h>

#include <iostream>
#include <string>

#include "Request.hpp"

class Helpers {
  public:
   static uint64_t GetResourceId(const char *url);
   static void SendRequestNop(struct io_uring *ring, struct Request *request, int timeout);
};
#endif
