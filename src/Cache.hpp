#ifndef SRC_CACHE_HPP_
#define SRC_CACHE_HPP_

#include <liburing.h>

#include <iostream>
#include <string>

#include "Request.hpp"

class Cache {
  public:
   Cache();
   void SetRing(struct io_uring *ring);

   void AddVerifyRequest();
   int HandleVerify();

   void AddExistsRequest(struct Request *request);
   int HandleExists(struct Request *request);

   void AddReadRequest(struct Request *request);
   int HandleRead(struct Request *request);

  private:
   struct io_uring *ring_;
   char *GetUID(char *url);
};
#endif
