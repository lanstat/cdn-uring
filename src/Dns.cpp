#include "Dns.hpp"

#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#include <fstream>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Request.hpp"
#include "Settings.hpp"
#include "Utils.hpp"

#define VERIFY_TIMEOUT 50000000  // 50 ms

Dns::Dns() {
   ipv4_regex_ =
       std::regex("^(((25[0-5]|(2[0-4]|1\\d|[1-9]|)\\d)\\.?\\b){4})(:\\d+)*$");
   ipv6_regex_ = std::regex("\\[([a-f0-9:]+:+)+[a-f0-9]+\\](:\\d+)*");

   LoadHostFile();
}

Dns::~Dns() {}

void Dns::SetRing(struct io_uring *ring) { ring_ = ring; }

int Dns::HandleVerifyUDP() {
   // checkQueries();
   parseEvent(1, IPv4Socket);
   parseEvent(1, IPv6Socket);
   return 0;
}

void Dns::AddVerifyUDPRequest(struct Request *request) {
   struct timespec rts;
   int ret = clock_gettime(CLOCK_MONOTONIC, &rts);
   if (ret < 0) {
      fprintf(stderr, "clock_gettime CLOCK_REALTIME error: %s\n",
              strerror(errno));
   }
   rts.tv_nsec += VERIFY_TIMEOUT;
   // rts.tv_sec += 5;

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_timeout(sqe, (struct __kernel_timespec *)&rts, 1,
                         IORING_TIMEOUT_ABS);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);
}

void Dns::AddFetchAAAARequest(struct Request *request, bool isHttps) {
   std::string host((char *)request->iov[2].iov_base);
   host = host.substr(1);
   std::size_t pos = host.find("/");
   if (pos != std::string::npos) {
      host = host.substr(0, pos);
   }
   int port = 80;
   if (isHttps) {
      port = 443;
   }

   if (hosts_ipv4_.find(host) != hosts_ipv4_.end()) {
      auto ip = hosts_ipv4_.at(host);
      struct sockaddr_in socket;
      socket.sin_port = htons(port);
      socket.sin_family = AF_INET;

      if (inet_pton(AF_INET, ip.c_str(), &socket.sin_addr) <= 0) {
         dnsError((void *)request);
         return;
      }

      dnsRight((void *)request, socket);
      return;
   }

   if (hosts_ipv6_.find(host) != hosts_ipv6_.end()) {
      auto ip = hosts_ipv6_.at(host);
      struct sockaddr_in6 socket;
      socket.sin6_port = htons(port);
      socket.sin6_family = AF_INET6;

      if (inet_pton(AF_INET6, host.c_str(), &socket.sin6_addr) <= 0) {
         dnsError((void *)request);
         return;
      }

      dnsRight((void *)request, socket);
      return;
   }

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
      pos = host.find("]");
      std::string tmp = host.substr(pos);
      host = host.substr(1, pos - 1);
      pos = tmp.find(":");
      if (pos != std::string::npos) {
         port = std::stoi(tmp.substr(pos + 1));
      }
      struct sockaddr_in6 socket;
      socket.sin6_port = htons(port);
      socket.sin6_family = AF_INET6;

      if (inet_pton(AF_INET6, host.c_str(), &socket.sin6_addr) <= 0) {
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

void Dns::LoadHostFile() {
   if (Settings::HostFile.empty()) {
      return;
   }
   std::ifstream file;
   file.open(Settings::HostFile);
   if (!file.is_open()) {
      Log(__FILE__, __LINE__, Log::kError) << "Error opening file";
      return;
   }
   std::string line;
   while (std::getline(file, line)) {
      std::size_t pos = line.find(" ");
      if (pos != std::string::npos) {
         std::string domain = line.substr(pos + 1);
         std::string ip = line.substr(0, pos);

         if (std::regex_match(ip, ipv4_regex_)) {
            std::pair<std::string, std::string> item(domain, ip);
            hosts_ipv4_.insert(item);
            continue;
         }
         if (std::regex_match(ip, ipv6_regex_)) {
            std::pair<std::string, std::string> item(domain, ip);
            hosts_ipv6_.insert(item);
            continue;
         }
      }
   }
   file.close();
}
