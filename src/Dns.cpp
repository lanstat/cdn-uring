#include "Dns.hpp"

#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Request.hpp"

#define VERIFY_TIMEOUT 50000000  // 50 ms

Dns::Dns() {}

Dns::~Dns() {}

void Dns::SetRing(struct io_uring *ring) { ring_ = ring; }

int Dns::HandleVerifyUDP() {
   // checkQueries();
   parseEvent(1, IPv4Socket);
   parseEvent(1, IPv6Socket);
   return 0;
}

void Dns::AddVerifyUDPRequest() {
   struct timespec rts;
   int ret = clock_gettime(CLOCK_MONOTONIC, &rts);
   if (ret < 0) {
      fprintf(stderr, "clock_gettime CLOCK_REALTIME error: %s\n",
              strerror(errno));
   }
   // rts.tv_nsec += VERIFY_TIMEOUT;
   rts.tv_sec += 1;
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   struct Request *req = (Request *)malloc(sizeof(*req) + sizeof(struct iovec));
   req->event_type = EVENT_TYPE_DNS_VERIFY;
   req->iovec_count = 0;

   io_uring_prep_timeout(sqe, (struct __kernel_timespec *)&rts, 1,
                         IORING_TIMEOUT_ABS);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);
}

void Dns::AddFetchAAAARequest(struct Request *request) {
   std::string host((char *)request->iov[0].iov_base);
   host = host.substr(1);
   std::size_t pos = host.find("/");
   host = host.substr(0, pos);

   GetAAAA(host, false);
}

int Dns::HandleFetchAAAA(struct Request *request) { return 0; }

void Dns::dnsRight(const sockaddr_in6 &sIPv6, bool https) {
   char astring[INET6_ADDRSTRLEN];
   inet_ntop(AF_INET6, &(sIPv6.sin6_addr), astring, INET6_ADDRSTRLEN);
   Log(__FILE__, __LINE__) << astring;
}

void Dns::dnsError() { Log(__FILE__, __LINE__, Log::kError) << "dns error"; }

void Dns::dnsWrong() { Log(__FILE__, __LINE__, Log::kError) << "dns wrong"; }
