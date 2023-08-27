#ifndef SRC_DNS_HPP_
#define SRC_DNS_HPP_

#include <liburing.h>

#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>

#include "DnsSeeker.hpp"
#include "Request.hpp"

class Dns : public DnsSeeker {
 public:
  Dns();
  virtual ~Dns();

  void SetRing(struct io_uring *ring);

  void AddVerifyUDPRequest(struct Request *request);
  int HandleVerifyUDP();

  void AddFetchAAAARequest(struct Request *request, bool isHttps);
  int HandleFetchAAAA(struct Request *request);

 private:
  struct io_uring *ring_;
  std::regex ipv4_regex_;
  std::regex ipv6_regex_;
  std::unordered_map<std::string, std::string> hosts_ipv4_;
  std::unordered_map<std::string, std::string> hosts_ipv6_;

  void LoadHostFile();

 protected:
  void dnsRight(const std::vector<void *> &requests,
                const sockaddr_in6 &sIPv6) override;
  void dnsRight(void *request, const sockaddr_in6 &sIPv6) override;
  void dnsRight(const std::vector<void *> &requests,
                const sockaddr_in &socket_v4) override;
  void dnsRight(void *request, const sockaddr_in &socket_v4) override;
  void dnsError() override;
  void dnsError(void *request) override;
  void dnsWrong() override;
};
#endif
