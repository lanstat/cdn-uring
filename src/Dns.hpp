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

   void AddFetchAAAARequest(struct Request *request, bool isHttps);
   int HandleFetchAAAA(struct Request *request);

  private:
   struct io_uring *ring_;

  protected:
   void dnsRight(const std::vector<void *> &requests,
                 const sockaddr_in6 &sIPv6) override;
   void dnsRight(void *request, const sockaddr_in6 &sIPv6) override;
   void dnsError() override;
   void dnsWrong() override;
};
#endif
