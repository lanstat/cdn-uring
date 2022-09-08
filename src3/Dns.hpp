#ifndef SRC_DNS_HPP_
#define SRC_DNS_HPP_

#include <netinet/in.h>
#include <sys/socket.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "DnsRequest.hpp"

#define CACHETIMEDIVIDER 1
// should be power of 2
#define MAXDNSSERVER 16

class Http;

class Dns {
   public:
    Dns();
    ~Dns();
    void parseEvent(const int &event, int socket);
    inline bool canAddToPos(const int &i, const int &size, int &pos);
    inline bool read8Bits(uint8_t &var, const char *const data, const int &size,
                          int &pos);
    inline bool read16Bits(uint16_t &var, const char *const data,
                           const int &size, int &pos);
    inline bool read16BitsRaw(uint16_t &var, const char *const data,
                              const int &size, int &pos);
    inline bool read32Bits(uint32_t &var, const char *const data,
                           const int &size, int &pos);
    bool tryOpenSocket();

    bool GetAAAA(DnsRequest *request, const std::string &host,
                 const bool &https);
    void cancelClient(Http *http, const std::string &host, const bool &https,
                      const bool &ignoreNotFound = false);
    int requestCountMerged();
    void cleanCache();
    void checkQueries();
    uint8_t serverCount() const;
    uint8_t retryBeforeError() const;
    uint8_t resendQueryDNS_ms() const;
    std::string getQueryList() const;
    int get_httpInProgress() const;
    static const unsigned char include[];
    static const unsigned char exclude[];

    void dnsRight(const sockaddr_in6 &sIPv6);
    void dnsError();
    void dnsWrong();
    uint64_t msFrom1970();

    in6_addr *GetEntry(const std::string &host);

    int IPv4Socket;
    int IPv6Socket;

   private:
    enum StatusEntry : uint8_t {
        StatusEntry_Right = 0x00,
        StatusEntry_Wrong = 0x01,
        StatusEntry_Error = 0x02,
        StatusEntry_Timeout = 0x03,
    };
    enum Mode : uint8_t {
        Mode_IPv6 = 0x00,
        Mode_IPv4 = 0x01,
    };
    struct CacheAAAAEntry {
        in6_addr sin6_addr;
        uint64_t outdated_date; /*in s from 1970*/
        StatusEntry status;
    };
    std::unordered_map<std::string, CacheAAAAEntry> cacheAAAA;
    std::map<uint64_t /*outdated_date in s from 1970*/,
             std::vector<std::string>>
        cacheAAAAByOutdatedDate;
    void addCacheEntryFailed(const StatusEntry &s, const uint32_t &ttl,
                             const std::string &host);
    void addCacheEntry(const StatusEntry &s, const uint32_t &ttl,
                       const std::string &host, const in6_addr &sin6_addr);

    struct DnsServerEntry {
        Mode mode;
        sockaddr_in6 targetDnsIPv6;
        sockaddr_in targetDnsIPv4;
        uint64_t lastFailed;  // 0 if never failed
    };
    std::vector<DnsServerEntry> dnsServerList;
    // to put on last try the dns server with problem
    uint8_t
        preferedServerOrder[MAXDNSSERVER];  // if MAXDNSSERVER <=16 then lower
                                            // memory on 64Bits system than
                                            // std::vector (std::vector memory =
                                            // 16bytes + X entry 1Byte/8Bits)
    uint16_t increment;

    struct Query {
        uint64_t startTimeInms;
        std::string host;
        // separate http and https to improve performance by better caching
        // socket to open
        std::vector<DnsRequest*> http;
        std::vector<DnsRequest*> https;
        uint8_t retryTime;  // number of time all query was send, now all query
                            // is send at time
        uint64_t nextRetry;
        std::string query;
    };
    int httpInProgress;
    void addQuery(const uint16_t &id, const Query &query);
    void removeQuery(const uint16_t &id, const bool &withNextDueTime = true);
    std::map<uint64_t, std::vector<uint16_t>> queryByNextDueTime;
    std::unordered_map<uint16_t, struct Query> queryList;
    std::unordered_map<std::string, uint16_t> queryListByHost;
    sockaddr_in6 targetHttp;
    sockaddr_in6 targetHttps;
    in6_addr sin6_addr;
};

#endif  // SRC_DNS_HPP_
