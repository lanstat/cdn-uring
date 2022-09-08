#ifndef SRC_DNS_HPP_
#define SRC_DNS_HPP_

#include <liburing.h>

#include <iostream>
#include <string>

#include "Request.hpp"

class Dns {
 public:
  Dns();

  void SetRing(struct io_uring *ring);

  void AddVerifyUDPRequest();
  int HandleVerifyUDP();

  void AddFetchAAAARequest(struct Request *request);
  int HandleFetchAAAA(struct Request *request);

 private:
  struct io_uring *ring_;
};
#endif
