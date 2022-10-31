#ifndef SRC_HTTP_HPP_
#define SRC_HTTP_HPP_

#include <liburing.h>

#include <iostream>
#include <string>

#include "Request.hpp"
#include "Server.hpp"

class Http {
  public:
   Http();

   void SetRing(struct io_uring *ring);
   void SetServer(Server *server);

   void AddFetchDataRequest(struct Request *req);
   int HandleFetchData(struct Request *request);
   void AddReadRequest(struct Request *request, int fd);

  private:
   struct io_uring *ring_;
   Server *server_;

   void Fetch(struct Request *request);
};
#endif
