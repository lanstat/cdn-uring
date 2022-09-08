#ifndef SRC_HTTP_HPP_
#define SRC_HTTP_HPP_

#include <liburing.h>

#include <iostream>
#include <string>

#include "Request.hpp"

class Http {
 public:
  Http();

  void SetRing(struct io_uring *ring);

  void AddFetchDataRequest(struct Request *req);
  int HandleFetchData(struct Request *request);

 private:
  struct io_uring *ring_;
};
#endif
