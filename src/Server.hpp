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
  void AddHttpErrorRequest(int client_socket, int status_code);
  bool HandleRead(struct Request *request, struct Request *inner_request);

  void AddWriteRequest(struct Request *request, bool is_stream);
  void HandleWrite(struct Request *request, int response);
  int HandleWriteStream(struct Request *request, int response);

  void AddCloseRequest(struct Request *request);
  void HandleClose(struct Request *request);

 private:
  struct io_uring *ring_;

  int FetchHeader(const char *src, char *command, char *header, int dest_sz);
};
#endif
