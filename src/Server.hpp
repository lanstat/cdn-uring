#ifndef SRC_SERVER_HPP_
#define SRC_SERVER_HPP_

#include <liburing.h>

#include <iostream>
#include <string>

#include "Request.hpp"

class Server {
  public:
   Server();
   void SetRing(struct io_uring *ring);

   void AddReadRequest(int client_socket);
   void AddHttpErrorRequest(struct Request *request, int status_code);
   bool HandleRead(struct Request *request, struct Request *inner_request);

   void AddWriteRequest(struct Request *request);
   void HandleWrite(struct Request *request);

  private:
   struct io_uring *ring_;

   int GetLine(const char *src, char *dest, int dest_sz);
};
#endif
