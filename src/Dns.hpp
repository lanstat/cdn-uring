#ifndef SRC_DNS_HPP_
#define SRC_DNS_HPP_

#include <liburing.h>

#include <iostream>
#include <string>

#include "DnsSeeker.hpp"
#include "Request.hpp"

class Dns : public DnsSeeker {
  public:
   Dns();
   virtual ~Dns();

   void SetRing(struct io_uring *ring);

   void AddVerifyUDPRequest();
   int HandleVerifyUDP();

   void AddFetchAAAARequest(struct Request *request);
   int HandleFetchAAAA(struct Request *request);

  private:
   struct io_uring *ring_;

  protected:
   void dnsRight(const sockaddr_in6 &sIPv6, bool https) override;
   void dnsError() override;
   void dnsWrong() override;
};
#endif
