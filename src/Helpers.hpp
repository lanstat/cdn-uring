#ifndef SRC_HELPERS_HPP_
#define SRC_HELPERS_HPP_

#include <liburing.h>

#include <iostream>
#include <string>

#include "Request.hpp"

class Helpers {
  public:
   static uint64_t GetResourceId(const char *url);
   static void Nop(struct io_uring *ring, struct Request *request, int timeout);
   static void CloseFD(int fd);
   static void CleanCache(uint64_t resource_id);
   static long GetTicks();
};
#endif
