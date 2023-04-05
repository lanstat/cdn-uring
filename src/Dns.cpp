#include "Dns.hpp"

#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Request.hpp"

#define VERIFY_TIMEOUT 50000000  // 50 ms

Dns::Dns() {
   ipv4_regex_ =
       std::regex("^(((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4})(:\\d+)*$");
   ipv6_regex_ = std::regex(
       "^%5B(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:"
       "|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:["
       "0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|"
       "([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,"
       "2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|"
       ":((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]"
       "{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-"
       "9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:)"
       "{1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]"
       "|1{0,1}[0-9]){0,1}[0-9]))%5D(:\\d+)*$");
}

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
   rts.tv_nsec += VERIFY_TIMEOUT;
   // rts.tv_sec += 5;
   struct Request *req = (Request *)malloc(sizeof(*req) + sizeof(struct iovec));
   req->event_type = EVENT_TYPE_DNS_VERIFY;
   req->iovec_count = 0;

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_timeout(sqe, (struct __kernel_timespec *)&rts, 1,
                         IORING_TIMEOUT_ABS);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);
}

void Dns::AddFetchAAAARequest(struct Request *request, bool isHttps) {
   std::string host((char *)request->iov[2].iov_base);
   host = host.substr(1);
   std::size_t pos = host.find("/");
   host = host.substr(0, pos);
   int port = 80;

   if (std::regex_match(host, ipv4_regex_)) {
      pos = host.find(":");
      if (pos != std::string::npos) {
         port = std::stoi(host.substr(pos + 1));
         host = host.substr(0, pos);
      }
      struct sockaddr_in socket;
      socket.sin_port = htons(port);
      socket.sin_family = AF_INET;

      if (inet_pton(AF_INET, host.c_str(), &socket.sin_addr) <= 0) {
         dnsError((void *)request);
         return;
      }

      dnsRight((void *)request, socket);
      return;
   }
   if (std::regex_match(host, ipv6_regex_)) {
      pos = host.find("%5D");
      std::string tmp = host.substr(pos);
      host = host.substr(3, pos - 3);
      pos = tmp.find(":");
      if (pos != std::string::npos) {
         port = std::stoi(tmp.substr(pos + 1));
      }
      struct sockaddr_in socket;
      socket.sin_port = htons(port);
      socket.sin_family = AF_INET6;

      if (inet_pton(AF_INET6, host.c_str(), &socket.sin_addr) <= 0) {
         dnsError((void *)request);
         return;
      }

      dnsRight((void *)request, socket);
      return;
   }

   GetAAAA((void *)request, host, isHttps);
}

int Dns::HandleFetchAAAA(struct Request *request) {
   Log(__FILE__, __LINE__) << "llego";
   return 0;
}

void Dns::dnsRight(const std::vector<void *> &requests,
                   const sockaddr_in6 &sIPv6) {
   unsigned int index = 0;
   while (index < requests.size()) {
      dnsRight(requests.at(index), sIPv6);

      index++;
   }
}

void Dns::dnsRight(void *record, const sockaddr_in6 &socket_v6) {
   struct Request *cache = (Request *)record;
   size_t size = sizeof(sockaddr_in6);
   cache->iov[4].iov_base = malloc(size);
   cache->iov[4].iov_len = size;
   memcpy(cache->iov[4].iov_base, &socket_v6, size);
   cache->event_type = EVENT_TYPE_HTTP_FETCH;

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_nop(sqe);
   io_uring_sqe_set_data(sqe, cache);
   io_uring_submit(ring_);
}

void Dns::dnsRight(const std::vector<void *> &requests,
                   const sockaddr_in &socket_v4) {}

void Dns::dnsRight(void *record, const sockaddr_in &socket_v4) {
   struct Request *cache = (Request *)record;
   size_t size = sizeof(sockaddr_in);
   cache->iov[4].iov_base = malloc(size);
   cache->iov[4].iov_len = size;
   memcpy(cache->iov[4].iov_base, &socket_v4, size);
   cache->event_type = EVENT_TYPE_HTTP_FETCH_IPV4;

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_nop(sqe);
   io_uring_sqe_set_data(sqe, cache);
   io_uring_submit(ring_);
}

void Dns::dnsError() { Log(__FILE__, __LINE__, Log::kError) << "dns error"; }

void Dns::dnsError(void *request) {
   Log(__FILE__, __LINE__, Log::kError) << "dns error";
}

void Dns::dnsWrong() { Log(__FILE__, __LINE__, Log::kError) << "dns wrong"; }
