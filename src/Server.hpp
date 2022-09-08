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
   struct Request *HandleRead(struct Request *request);

   void AddWriteRequest(struct Request *request);
   int HandleWrite(struct Request *request);

  private:
   struct io_uring *ring_;

   void HandleHttpMethod(char *method_buffer, int client_socket);
   int GetLine(const char *src, char *dest, int dest_sz);
   void SendStaticStringContent(const char *str, int client_socket);
   void HandleUnimplementedMethod(int client_socket);
};
#endif
