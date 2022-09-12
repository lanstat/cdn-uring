#include "DnsSeeker.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>

#include "Logger.hpp"

struct __attribute__((__packed__)) dns_query {
    uint16_t id;
    uint16_t flags;
    uint16_t question_count;
    uint16_t answer_count;
    uint16_t authority_count;
    uint16_t add_count;
    uint8_t payload[];
};

const unsigned char DnsSeeker::include[] = {0x28, 0x03, 0x19, 0x20};
const unsigned char DnsSeeker::exclude[] = {0x28, 0x03, 0x19, 0x20, 0x00,
                                            0x00, 0x00, 0x00, 0xb4, 0xb2,
                                            0x5f, 0x61, 0xd3, 0x7f};

DnsSeeker::DnsSeeker() {
    memset(&targetHttp, 0, sizeof(targetHttp));
    targetHttp.sin6_port = htobe16(80);
    targetHttp.sin6_family = AF_INET6;

    memset(&targetHttps, 0, sizeof(targetHttps));
    targetHttps.sin6_port = htobe16(443);
    targetHttps.sin6_family = AF_INET6;

    httpInProgress = 0;
    IPv4Socket = 0;
    IPv6Socket = 0;

    uint8_t indexPreferedServerOrder = 0;

    memset(&sin6_addr, 0, sizeof(sin6_addr));
    if (!tryOpenSocket()) {
        std::cerr << "tryOpenSocket() failed (abort)" << std::endl;
        abort();
    }

    // read resolv.conf
    {
        FILE *fp;
        char *line = NULL;
        size_t len = 0;
        ssize_t read;

        fp = fopen("/etc/resolv.conf", "r");
        if (fp == NULL) {
            std::cerr << "Unable to open /etc/resolv.conf" << std::endl;
            exit(EXIT_FAILURE);
        }

        while ((read = getline(&line, &len, fp)) != -1) {
            // create udp socket to dns server

            std::string line2(line);
            std::string prefix = line2.substr(0, 11);
            if (prefix == "nameserver ") {
                line2 = line2.substr(11);
                line2.resize(line2.size() - 1);
                const std::string &host = line2;

                sockaddr_in6 targetDnsIPv6;
                memset(&targetDnsIPv6, 0, sizeof(targetDnsIPv6));
                targetDnsIPv6.sin6_port = htobe16(53);
                const char *const hostC = host.c_str();
                int convertResult =
                    inet_pton(AF_INET6, hostC, &targetDnsIPv6.sin6_addr);
                if (convertResult != 1) {
                    sockaddr_in targetDnsIPv4;
                    memset(&targetDnsIPv4, 0, sizeof(targetDnsIPv4));
                    targetDnsIPv4.sin_port = htobe16(53);
                    convertResult =
                        inet_pton(AF_INET, hostC, &targetDnsIPv4.sin_addr);
                    if (convertResult != 1) {
                        std::cerr << "not IPv4 and IPv6 address, host: \""
                                  << host << "\", portstring: \"53\", errno: "
                                  << std::to_string(errno) << std::endl;
                        abort();
                    } else {
                        targetDnsIPv4.sin_family = AF_INET;

                        DnsServerEntry e;
                        e.mode = Mode_IPv4;
                        memcpy(&e.targetDnsIPv4, &targetDnsIPv4,
                               sizeof(targetDnsIPv4));
                        memset(&e.targetDnsIPv6, 0, sizeof(e.targetDnsIPv6));
                        e.lastFailed = 0;
                        dnsServerList.push_back(e);
                        preferedServerOrder[indexPreferedServerOrder] =
                            indexPreferedServerOrder;
                        indexPreferedServerOrder++;
                    }
                } else {
                    targetDnsIPv6.sin6_family = AF_INET6;

                    DnsServerEntry e;
                    e.mode = Mode_IPv6;
                    memcpy(&e.targetDnsIPv6, &targetDnsIPv6,
                           sizeof(targetDnsIPv6));
                    memset(&e.targetDnsIPv4, 0, sizeof(e.targetDnsIPv4));
                    e.lastFailed = 0;
                    dnsServerList.push_back(e);
                    preferedServerOrder[indexPreferedServerOrder] =
                        indexPreferedServerOrder;
                    indexPreferedServerOrder++;
                }
            }
        }

        fclose(fp);
        if (line) free(line);
    }
    while (indexPreferedServerOrder < MAXDNSSERVER) {
        preferedServerOrder[indexPreferedServerOrder] = 0;
        indexPreferedServerOrder++;
    }

    if (dnsServerList.empty()) {
        std::cerr << "Sorry but the server list is empty" << std::endl;
        abort();
    }
    increment = 1;
}

DnsSeeker::~DnsSeeker() {}

bool DnsSeeker::tryOpenSocket() {
    {
        const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd == -1) {
            std::cerr << "unable to create UDP socket" << std::endl;
            abort();
        }
        sockaddr_in si_me;
        memset((char *)&si_me, 0, sizeof(si_me));
        si_me.sin_family = AF_INET;
        si_me.sin_port = htons(50053);
        si_me.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd, (struct sockaddr *)&si_me, sizeof(si_me)) == -1) {
            std::cerr << "unable to bind UDP socket, errno: " << errno
                      << std::endl;
            abort();
        }

        int flags, s;
        flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
            std::cerr << "fcntl get flags error" << std::endl;
        else {
            flags |= O_NONBLOCK;
            s = fcntl(fd, F_SETFL, flags);
            if (s == -1) std::cerr << "fcntl set flags error" << std::endl;
        }
        IPv4Socket = fd;
    }
    {
        const int fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (fd == -1) {
            std::cerr << "unable to create UDP socket" << std::endl;
            abort();
        }
        sockaddr_in6 si_me;
        memset((char *)&si_me, 0, sizeof(si_me));
        si_me.sin6_family = AF_INET6;
        si_me.sin6_port = htons(50054);
        si_me.sin6_addr = IN6ADDR_ANY_INIT;
        if (bind(fd, (struct sockaddr *)&si_me, sizeof(si_me)) == -1) {
            std::cerr << "unable to bind UDP socket, errno: " << errno
                      << std::endl;
            abort();
        }

        int flags, s;
        flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
            std::cerr << "fcntl get flags error" << std::endl;
        else {
            flags |= O_NONBLOCK;
            s = fcntl(fd, F_SETFL, flags);
            if (s == -1) std::cerr << "fcntl set flags error" << std::endl;
        }
        IPv6Socket = fd;
    }
    return true;
}

/// Procesa un evento de uring
void DnsSeeker::parseEvent(const int &event, int socket) {
    if (true) {
        int size = 0;
        do {
            char buffer[1500];
            sockaddr_in6 si_other6;
            sockaddr_in si_other4;
            if (socket == IPv6Socket) {
                unsigned int slen = sizeof(si_other6);
                memset(&si_other6, 0, sizeof(si_other6));
                size = recvfrom(socket, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&si_other6, &slen);
                if (size < 0) {
                    break;
                }
            } else if (socket == IPv4Socket) {
                unsigned int slen = sizeof(si_other4);
                memset(&si_other4, 0, sizeof(si_other4));
                size = recvfrom(socket, buffer, sizeof(buffer), 0,
                                (struct sockaddr *)&si_other4, &slen);
                if (size < 0) {
                    break;
                }
            } else {
                std::cerr << "DnsSeeker::parseEvent() unknown socket"
                          << std::endl;
                return;
            }

            int pos = 0;
            uint16_t transactionId = 0;
            if (!read16BitsRaw(transactionId, buffer, size, pos)) return;
            uint16_t flags = 0;
            if (!read16Bits(flags, buffer, size, pos)) return;
            uint16_t questions = 0;
            if (!read16Bits(questions, buffer, size, pos)) return;
            uint16_t answersIndex = 0;
            uint16_t answers = 0;
            if (!read16Bits(answers, buffer, size, pos)) return;
            if (!canAddToPos(2 + 2, size, pos)) return;

            // skip query
            uint8_t len, offs = 0;
            while ((offs < (size - pos)) && (len = buffer[pos + offs]))
                offs += len + 1;
            pos += offs + 1;
            uint16_t type = 0;
            if (!read16Bits(type, buffer, size, pos)) return;
            if (type != 0x001c) return;
            uint16_t classIn = 0;
            if (!read16Bits(classIn, buffer, size, pos)) return;
            if (classIn != 0x0001) return;

            // answers list
            if (queryList.find(transactionId) != queryList.cend()) {
                const Query &q = queryList.at(transactionId);

                if (socket == IPv6Socket) {
                    unsigned int index = 0;
                    while (index < dnsServerList.size()) {
                        const DnsServerEntry &dnsServer =
                            dnsServerList.at(index);
                        if (dnsServer.mode == Mode_IPv6 &&
                            memcmp(&dnsServer.targetDnsIPv6.sin6_addr,
                                   &si_other6.sin6_addr, 16) == 0)
                            break;
                        index++;
                    }
                    if (index >= dnsServerList.size()) {
                        return;
                    }
                } else {
                    unsigned int index = 0;
                    while (index < dnsServerList.size()) {
                        const DnsServerEntry &dnsServer =
                            dnsServerList.at(index);
                        if (dnsServer.mode == Mode_IPv4 &&
                            memcmp(&dnsServer.targetDnsIPv4.sin_addr,
                                   &si_other4.sin_addr, 4) == 0)
                            break;
                        index++;
                    }
                    if (index >= dnsServerList.size()) {
                        return;
                    }
                }
                if (httpInProgress > 0) httpInProgress--;

                const std::vector<int> &http = q.http;
                const std::vector<int> &https = q.https;
                // std::string hostcpp(std::string hostcpp(q.host));-> not
                // needed
                if (!http.empty() || !https.empty()) {
                    bool clientsFlushed = false;

                    if ((flags & 0x000F) == 0x0001) {
                        if (!clientsFlushed) {
                            clientsFlushed = true;
                            // addCacheEntry(StatusEntry_Wrong,0,q.host);->
                            // wrong string to resolve, host is not dns valid
                            bool cacheFound = false;
                            if (cacheAAAA.find(q.host) != cacheAAAA.cend()) {
                                CacheAAAAEntry &entry = cacheAAAA.at(q.host);
                                uint64_t t = time(NULL);
                                const uint64_t &maxTime = t + 24 * 3600;
                                // fix time drift
                                if (entry.outdated_date > maxTime)
                                    entry.outdated_date = maxTime;

                                if (entry.status == StatusEntry_Right) {
                                    if (!https.empty()) {
                                        memcpy(&targetHttps.sin6_addr,
                                               &entry.sin6_addr, 16);
                                        dnsRight(targetHttps, true);
                                    }
                                    if (!http.empty()) {
                                        memcpy(&targetHttp.sin6_addr,
                                               &entry.sin6_addr, 16);
                                        dnsRight(targetHttp, false);
                                    }
                                    cacheFound = true;
                                }
                            }
                            if (cacheFound == false) {
                                dnsError();
                            }
                            removeQuery(transactionId);
                        }
                    } else if ((flags & 0xFA0F) != 0x8000) {
                        if (!clientsFlushed) {
                            clientsFlushed = true;
                            bool cacheFound = false;
                            if (cacheAAAA.find(q.host) != cacheAAAA.cend()) {
                                CacheAAAAEntry &entry = cacheAAAA.at(q.host);
                                uint64_t t = time(NULL);
                                const uint64_t &maxTime = t + 24 * 3600;
                                // fix time drift
                                if (entry.outdated_date > maxTime)
                                    entry.outdated_date = maxTime;
                                if (entry.status == StatusEntry_Right) {
                                    if (!https.empty()) {
                                        memcpy(&targetHttps.sin6_addr,
                                               &entry.sin6_addr, 16);
                                        dnsRight(targetHttps, true);
                                    }
                                    if (!http.empty()) {
                                        memcpy(&targetHttp.sin6_addr,
                                               &entry.sin6_addr, 16);
                                        dnsRight(targetHttp, false);
                                    }
                                    cacheFound = true;
                                }
                            }
                            if (cacheFound == false) {
                                addCacheEntryFailed(StatusEntry_Wrong, 300,
                                                    q.host);
                                dnsError();
                            }
                            removeQuery(transactionId);
                        }
                    } else {
                        while (answersIndex < answers) {
                            uint16_t AName = 0;
                            if (!read16Bits(AName, buffer, size, pos)) {
                                return;
                            }
                            uint16_t type = 0;
                            if (!read16Bits(type, buffer, size, pos))
                                if (!clientsFlushed) {
                                    clientsFlushed = true;
                                    bool cacheFound = false;
                                    if (cacheAAAA.find(q.host) !=
                                        cacheAAAA.cend()) {
                                        CacheAAAAEntry &entry =
                                            cacheAAAA.at(q.host);
                                        uint64_t t = time(NULL);
                                        const uint64_t &maxTime = t + 24 * 3600;
                                        // fix time drift
                                        if (entry.outdated_date > maxTime)
                                            entry.outdated_date = maxTime;
                                        if (entry.status == StatusEntry_Right) {
                                            if (!https.empty()) {
                                                memcpy(&targetHttps.sin6_addr,
                                                       &entry.sin6_addr, 16);
                                                dnsRight(targetHttps, true);
                                            }
                                            if (!http.empty()) {
                                                memcpy(&targetHttp.sin6_addr,
                                                       &entry.sin6_addr, 16);
                                                dnsRight(targetHttp, false);
                                            }
                                            cacheFound = true;
                                        }
                                    }
                                    if (cacheFound == false) {
                                        addCacheEntryFailed(StatusEntry_Error,
                                                            300, q.host);
                                        dnsError();
                                    }
                                    removeQuery(transactionId);
                                }
                            switch (type) {
                                // AAAA
                                case 0x001c: {
                                    uint16_t classIn = 0;
                                    if (!read16Bits(classIn, buffer, size,
                                                    pos)) {
                                        return;
                                    }
                                    if (classIn != 0x0001) {
                                        break;
                                    }
                                    uint32_t ttl = 0;
                                    if (!read32Bits(ttl, buffer, size, pos)) {
                                        return;
                                    }
                                    uint16_t datasize = 0;
                                    if (!read16Bits(datasize, buffer, size,
                                                    pos)) {
                                        return;
                                    }
                                    if (datasize != 16) {
                                        return;
                                    }

                                    // TODO saveToCache();
                                    if (memcmp(buffer + pos, DnsSeeker::include,
                                               sizeof(DnsSeeker::include)) !=
                                            0 ||
                                        memcmp(buffer + pos, DnsSeeker::exclude,
                                               sizeof(DnsSeeker::exclude)) ==
                                            0) {
                                        if (!clientsFlushed) {
                                            clientsFlushed = true;
                                            addCacheEntry(
                                                StatusEntry_Wrong, ttl, q.host,
                                                *reinterpret_cast<in6_addr *>(
                                                    buffer + pos));
                                            dnsWrong();
                                            removeQuery(transactionId);
                                        }
                                    } else {
                                        if (!clientsFlushed) {
                                            clientsFlushed = true;
                                            addCacheEntry(
                                                StatusEntry_Right, ttl, q.host,
                                                *reinterpret_cast<in6_addr *>(
                                                    buffer + pos));

                                            if (!http.empty()) {
                                                memcpy(&targetHttp.sin6_addr,
                                                       buffer + pos, 16);
                                                dnsRight(targetHttp, false);
                                            }
                                            if (!https.empty()) {
                                                memcpy(&targetHttps.sin6_addr,
                                                       buffer + pos, 16);
                                                dnsRight(targetHttps, true);
                                            }
                                            removeQuery(transactionId);
                                        }
                                    }
                                } break;
                                default: {
                                    canAddToPos(2 + 4, size, pos);
                                    uint16_t datasize = 0;
                                    if (!read16Bits(datasize, buffer, size,
                                                    pos)) {
                                        return;
                                    }
                                    canAddToPos(datasize, size, pos);
                                } break;
                            }
                            answersIndex++;
                        }
                        if (!clientsFlushed) {
                            clientsFlushed = true;
                            bool cacheFound = false;
                            if (cacheAAAA.find(q.host) != cacheAAAA.cend()) {
                                CacheAAAAEntry &entry = cacheAAAA.at(q.host);
                                uint64_t t = time(NULL);
                                const uint64_t &maxTime = t + 24 * 3600;
                                // fix time drift
                                if (entry.outdated_date > maxTime)
                                    entry.outdated_date = maxTime;
                                if (entry.status == StatusEntry_Right) {
                                    if (!https.empty()) {
                                        memcpy(&targetHttps.sin6_addr,
                                               &entry.sin6_addr, 16);
                                        dnsRight(targetHttps, true);
                                    }
                                    if (!http.empty()) {
                                        memcpy(&targetHttp.sin6_addr,
                                               &entry.sin6_addr, 16);
                                        dnsRight(targetHttp, false);
                                    }
                                    cacheFound = true;
                                }
                            }
                            if (cacheFound == false) {
                                addCacheEntryFailed(StatusEntry_Error, 300,
                                                    q.host);
                                dnsError();
                            }
                            removeQuery(transactionId);
                        }
                    }
                }
            }
        } while (size >= 0);
    }
}

/*
void DnsSeeker::cleanCache() {
    if (cacheAAAA.size() < 100000) return;
    const std::map<uint64_t ,
                   std::vector<std::string>>
        cacheByOutdatedDate = this->cacheAAAAByOutdatedDate;
    for (auto const &x : cacheByOutdatedDate) {
        const uint64_t t = x.first;
        if (t > (uint64_t)time(NULL)) return;
        const std::vector<std::string> &list = x.second;
        for (auto const &host : list) cacheAAAA.erase(host);
        this->cacheAAAAByOutdatedDate.erase(t);
        if (cacheAAAA.size() < 100000) return;
    }
}
*/

void DnsSeeker::addCacheEntryFailed(const StatusEntry &s, const uint32_t &ttl,
                                    const std::string &host) {
    if (ttl < 600)  // always retry after 10min max
        addCacheEntry(s, ttl, host, sin6_addr);
    else
        addCacheEntry(s, 600, host, sin6_addr);
}

void DnsSeeker::addCacheEntry(const StatusEntry &s, const uint32_t &ttl,
                              const std::string &host,
                              const in6_addr &sin6_addr) {
    // prevent DDOS due to out of memory situation
    if (cacheAAAA.size() > 120000) return;

    // remove old entry from cacheByOutdatedDate
    if (cacheAAAA.find(host) != cacheAAAA.cend()) {
        const CacheAAAAEntry &e = cacheAAAA.at(host);
        std::vector<std::string> &list =
            cacheAAAAByOutdatedDate[e.outdated_date];
        for (size_t i = 0; i < list.size(); i++) {
            const std::string &s = list.at(i);
            if (s == host) {
                list.erase(list.cbegin() + i);
                break;
            }
        }
    }

    CacheAAAAEntry &entry = cacheAAAA[host];
    // normal case: check time minimum each 5min, maximum 24h
    if (s == StatusEntry_Right) {
        if (ttl < 5 * 60)
            entry.outdated_date = time(NULL) + 5 * 60 / CACHETIMEDIVIDER;
        else if (ttl < 24 * 3600)
            entry.outdated_date = time(NULL) + ttl / CACHETIMEDIVIDER;
        else
            entry.outdated_date = time(NULL) + 24 * 3600 / CACHETIMEDIVIDER;
    } else  // error case: check time minimum each 10s, maximum 10min
    {
        if (ttl < 10)
            entry.outdated_date = time(NULL) + 10 / CACHETIMEDIVIDER;
        else if (ttl < 600)
            entry.outdated_date = time(NULL) + ttl / CACHETIMEDIVIDER;
        else
            entry.outdated_date = time(NULL) + 600 / CACHETIMEDIVIDER;
    }
    entry.status = s;

    memcpy(&entry.sin6_addr, &sin6_addr, sizeof(in6_addr));

    // insert entry to cacheByOutdatedDate
    cacheAAAAByOutdatedDate[entry.outdated_date].push_back(host);
}

bool DnsSeeker::canAddToPos(const int &i, const int &size, int &pos) {
    if ((pos + i) > size) return false;
    pos += i;
    return true;
}

bool DnsSeeker::read8Bits(uint8_t &var, const char *const data, const int &size,
                          int &pos) {
    if ((pos + (int)sizeof(var)) > size) return false;
    var = data[pos];
    pos += sizeof(var);
    return true;
}

bool DnsSeeker::read16Bits(uint16_t &var, const char *const data,
                           const int &size, int &pos) {
    uint16_t t = 0;
    read16BitsRaw(t, data, size, pos);
    var = be16toh(t);
    return var;
}

bool DnsSeeker::read16BitsRaw(uint16_t &var, const char *const data,
                              const int &size, int &pos) {
    if ((pos + (int)sizeof(var)) > size) return false;
    memcpy(&var, data + pos, sizeof(var));
    pos += sizeof(var);
    return true;
}

bool DnsSeeker::read32Bits(uint32_t &var, const char *const data,
                           const int &size, int &pos) {
    if ((pos + (int)sizeof(var)) > size) return false;
    uint32_t t;
    memcpy(&t, data + pos, sizeof(var));
    var = be32toh(t);
    pos += sizeof(var);
    return true;
}

bool DnsSeeker::GetAAAA(const std::string &host, const bool &https) {
    if (dnsServerList.empty()) {
        std::cerr << "Sorry but the server list is empty" << std::endl;
        abort();
    }
    bool forceCache = false;
    if (queryListByHost.find(host) != queryListByHost.cend()) {
        const uint16_t &queryId = queryListByHost.at(host);
        if (queryList.find(queryId) != queryList.cend()) {
            Query &q = queryList[queryId];
            if (q.host == host) {
                if (q.retryTime >= DnsSeeker::retryBeforeError() &&
                    cacheAAAA.find(host) != cacheAAAA.cend())
                    forceCache = true;
                else {
                    if (https) {
                        q.https.push_back(1);
                    } else {
                        q.http.push_back(1);
                    }
                    return true;
                }
            } else {
                queryListByHost.erase(host);
            }
        } else  // bug, try fix
        {
            queryListByHost.erase(host);
        }
    }
    if (cacheAAAA.find(host) != cacheAAAA.cend()) {
        CacheAAAAEntry &entry = cacheAAAA.at(host);
        uint64_t t = time(NULL);
        if (entry.outdated_date > t || forceCache) {
            const uint64_t &maxTime = t + 24 * 3600;
            // fix time drift
            if (entry.outdated_date > maxTime) entry.outdated_date = maxTime;
            switch (entry.status) {
                case StatusEntry_Right:
                    if (https) {
                        memcpy(&targetHttps.sin6_addr, &entry.sin6_addr, 16);
                        dnsRight(targetHttps, true);
                    } else {
                        memcpy(&targetHttp.sin6_addr, &entry.sin6_addr, 16);
                        dnsRight(targetHttp, false);
                    }
                    break;
                default:
                case StatusEntry_Error:
                    dnsError();
                    break;
                case StatusEntry_Wrong: {
                    dnsWrong();
                } break;
                case StatusEntry_Timeout: {
                    dnsWrong();
                } break;
            }
            return true;
        }
    }
    if (httpInProgress > 1000) {
        return false;
    }
    // std::cout << "dns query count merged in progress>1000" << std::endl;
    uint8_t buffer[4096];
    struct dns_query *query = (struct dns_query *)buffer;
    query->id = increment++;
    if (increment > 65534) increment = 1;
    query->flags = htobe16(288);
    query->question_count = htobe16(1);
    query->answer_count = 0;
    query->authority_count = 0;
    query->add_count = 0;
    int pos = 2 + 2 + 2 + 2 + 2 + 2;

    // hostname encoded
    int hostprevpos = 0;
    size_t hostpos = host.find(".", hostprevpos);
    while (hostpos != std::string::npos) {
        const std::string &part =
            host.substr(hostprevpos, hostpos - hostprevpos);
        // std::cout << part << std::endl;
        buffer[pos] = part.size();
        pos += 1;
        memcpy(buffer + pos, part.data(), part.size());
        pos += part.size();
        hostprevpos = hostpos + 1;
        hostpos = host.find(".", hostprevpos);
    }
    const std::string &part = host.substr(hostprevpos);
    // std::cout << part << std::endl;
    buffer[pos] = part.size();
    pos += 1;
    memcpy(buffer + pos, part.data(), part.size());
    pos += part.size();

    buffer[pos] = 0x00;
    pos += 1;

    // type AAAA
    buffer[pos] = 0x00;
    pos += 1;
    buffer[pos] = 0x1c;
    pos += 1;

    // class IN
    buffer[pos] = 0x00;
    pos += 1;
    buffer[pos] = 0x01;
    pos += 1;

    Query queryToPush;
    queryToPush.host = host;
    queryToPush.retryTime = 0;
    queryToPush.startTimeInms = msFrom1970();
    queryToPush.nextRetry = msFrom1970() + resendQueryDNS_ms();
    queryToPush.query = std::string((char *)buffer, pos);

    bool sendOk = false;
    unsigned int serverDNSindex = 0;
    while (serverDNSindex < dnsServerList.size()) {
        const DnsServerEntry &dnsServer = dnsServerList.at(serverDNSindex);
        if (dnsServer.mode == Mode_IPv6) {
            const int result =
                sendto(IPv6Socket, &buffer, pos, 0,
                       (struct sockaddr *)&dnsServer.targetDnsIPv6,
                       sizeof(dnsServer.targetDnsIPv6));
            if (result != pos) {
            } else {
                sendOk = true;
            }
        } else  // if(mode==Mode_IPv4)
        {
            const int result =
                sendto(IPv4Socket, &buffer, pos, 0,
                       (struct sockaddr *)&dnsServer.targetDnsIPv4,
                       sizeof(dnsServer.targetDnsIPv4));
            if (result != pos) {
            } else {
                sendOk = true;
            }
        }

        serverDNSindex++;
    }
    if (!sendOk) {
        bool cacheFound = false;
        if (cacheAAAA.find(host) != cacheAAAA.cend()) {
            CacheAAAAEntry &entry = cacheAAAA[host];
            uint64_t t = time(NULL);
            const uint64_t &maxTime = t + 24 * 3600;
            // fix time drift
            if (entry.outdated_date > maxTime) entry.outdated_date = maxTime;
            if (entry.status == StatusEntry_Right) {
                if (https) {
                    memcpy(&targetHttps.sin6_addr, &entry.sin6_addr, 16);
                    dnsRight(targetHttps, true);
                } else {
                    memcpy(&targetHttp.sin6_addr, &entry.sin6_addr, 16);
                    dnsRight(targetHttp, false);
                }
                return true;
            }
        }
        if (cacheFound == false) {
            dnsError();
            addCacheEntryFailed(StatusEntry_Timeout, 30, host);
            return false;
        }
    }

    if (https)
        queryToPush.https.push_back(1);
    else
        queryToPush.http.push_back(1);
    Log(__FILE__, __LINE__) << queryToPush.host;
    addQuery(query->id, queryToPush);
    return true;
}

void DnsSeeker::addQuery(const uint16_t &id, const Query &query) {
    queryList[id] = query;
    queryListByHost[query.host] = id;
    queryByNextDueTime[query.nextRetry].push_back(id);
    if (httpInProgress < 2000000000) httpInProgress++;
}

void DnsSeeker::removeQuery(const uint16_t &id, const bool &withNextDueTime) {
    const Query &query = queryList.at(id);
    if (withNextDueTime) {
        if (queryByNextDueTime.find(query.nextRetry) ==
            queryByNextDueTime.cend())
            std::cerr << __FILE__ << ":" << __LINE__ << " query " << id
                      << " not found into queryByNextDueTime: "
                      << query.nextRetry << std::endl;
        queryByNextDueTime.erase(query.nextRetry);
    }
    if (queryByNextDueTime.find(query.nextRetry) == queryByNextDueTime.cend()) {
        Log(__FILE__, __LINE__, Log::kError)
            << "query " << id
            << " not found into queryListByHost: " << query.host;
    }
    queryListByHost.erase(query.host);
    if (queryByNextDueTime.find(query.nextRetry) == queryByNextDueTime.cend()) {
        Log(__FILE__, __LINE__, Log::kError)
            << "query " << id << " not found into queryList";
    }
    queryList.erase(id);
    if (httpInProgress > 0) httpInProgress--;
}

void DnsSeeker::cancelClient(const std::string &host, const bool &https,
                             const bool &ignoreNotFound) {
    if (queryListByHost.find(host) != queryListByHost.cend()) {
        const uint16_t queryId = queryListByHost.at(host);
        if (queryList.find(queryId) != queryList.cend()) {
            if (https) {
                std::vector<int> &httpsList = queryList[queryId].https;
                unsigned int index = 0;
                while (index < httpsList.size()) {
                    if (1 == httpsList.at(index)) {
                        httpsList.erase(httpsList.cbegin() + index);
                        break;
                    }
                    index++;
                }
            } else {
                std::vector<int> &httpList = queryList[queryId].http;
                unsigned int index = 0;
                while (index < httpList.size()) {
                    if (1 == httpList.at(index)) {
                        httpList.erase(httpList.cbegin() + index);
                        break;
                    }
                    index++;
                }
            }
            return;
        } else {
            // bug, try fix
            queryListByHost.erase(host);

            std::cerr << __FILE__ << ":" << __LINE__ << " try remove: "
                      << "asdasd"
                      << " to \"" << host << "\" but queryListByHost seam wrong"
                      << std::endl;
            abort();
        }
    } else {
        if (!ignoreNotFound) abort();
    }
}

/*
int DnsSeeker::requestCountMerged() { return queryListByHost.size(); }

std::string DnsSeeker::getQueryList() const {
    std::string ret;

    ret += "[";
    unsigned int index = 0;
    while (index < dnsServerList.size()) {
        if (index != 0) ret += ",";
        if (index < sizeof(preferedServerOrder)) {
            if (preferedServerOrder[index] < dnsServerList.size()) {
                const DnsServerEntry &d =
                    dnsServerList.at(preferedServerOrder[index]);
                if (d.mode == Mode_IPv6) {
                    char str[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, &d.targetDnsIPv6.sin6_addr, str,
                              INET6_ADDRSTRLEN);
                    ret += str;
                } else {
                    char str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &d.targetDnsIPv4.sin_addr, str,
                              INET_ADDRSTRLEN);
                    ret += str;
                }
            } else
                ret += "preferedServerOrder[" + std::to_string(index) +
                       "]:" + std::to_string(preferedServerOrder[index]) +
                       ">=" + std::to_string(dnsServerList.size());
        } else
            ret += "preferedServerOrder: " + std::to_string(index) +
                   ">=" + std::to_string(sizeof(preferedServerOrder));
        index++;
    }
    ret += "]\r\n";

    ret += "Dns queries (" + std::to_string(this->queryList.size()) +
           "): " + std::to_string(this->queryList.size()) + "\r\n";
    const std::unordered_map<uint16_t, Query> queryByNextDueTime =
        this->queryList;
    for (auto const &x : queryByNextDueTime) {
        ret += std::to_string(x.first) + ") ";
        const Query &q = x.second;
        ret += q.host;
        if (q.https.size() > 0) {
            ret += " (http:";
            unsigned int index = 0;
            while (index < q.http.size()) {
                std::string ret;
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%p",
                              (void *)q.http.at(index));
                ret += " " + std::string(buffer);
                index++;
            }
            ret += ")";
        }
        if (q.https.size() > 0) {
            ret += " (https:";
            unsigned int index = 0;
            while (index < q.https.size()) {
                std::string ret;
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%p",
                              (void *)q.https.at(index));
                ret += " " + std::string(buffer);
                index++;
            }
            ret += ")";
        }
        ret += " " + std::to_string(q.nextRetry) + " " +
               std::to_string(q.retryTime) + " ";
        ret += "[";
        index = 0;
        while (index < dnsServerList.size()) {
            if (index != 0) ret += ",";
            const DnsServerEntry &d = dnsServerList.at(index);
            if (d.mode == Mode_IPv6) {
                char str[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &d.targetDnsIPv6.sin6_addr, str,
                          INET6_ADDRSTRLEN);
                ret += str;
            } else {
                char str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &d.targetDnsIPv4.sin_addr, str,
                          INET_ADDRSTRLEN);
                ret += str;
            }
            index++;
        }
        ret += "]";
        ret += "\r\n";
    }

    return ret;
}

int DnsSeeker::get_httpInProgress() const {
    if (httpInProgress > 0)
        return httpInProgress;
    else
        return 0;
}

uint8_t DnsSeeker::serverCount() const { return dnsServerList.size(); }
*/

uint8_t DnsSeeker::retryBeforeError() const { return 4; }

uint8_t DnsSeeker::resendQueryDNS_ms() const { return 200; }

void DnsSeeker::checkQueries() {
    const std::map<uint64_t, std::vector<uint16_t>> queryByNextDueTime =
        this->queryByNextDueTime;
    for (auto const &x : queryByNextDueTime) {
        const uint64_t t = x.first;
        if (t > msFrom1970()) return;
        const std::vector<uint16_t> &list = x.second;
        for (auto const &id : list) {
            Query &query = queryList.at(id);

            bool sendOk = false;

            query.retryTime++;
            if (query.retryTime >= DnsSeeker::retryBeforeError() &&
                (!query.http.empty() || !query.http.empty())) {
                const std::vector<int> &http = query.http;
                const std::vector<int> &https = query.https;
                bool cacheFound = false;
                if (cacheAAAA.find(query.host) != cacheAAAA.cend()) {
                    CacheAAAAEntry &entry = cacheAAAA.at(query.host);
                    uint64_t t = time(NULL);
                    const uint64_t &maxTime = t + 24 * 3600;
                    // fix time drift
                    if (entry.outdated_date > maxTime)
                        entry.outdated_date = maxTime;
                    if (entry.status == StatusEntry_Right) {
                        if (!https.empty()) {
                            memcpy(&targetHttps.sin6_addr, &entry.sin6_addr,
                                   16);
                            dnsRight(targetHttps, true);
                        }
                        if (!http.empty()) {
                            memcpy(&targetHttp.sin6_addr, &entry.sin6_addr, 16);
                            dnsRight(targetHttp, false);
                        }
                        cacheFound = true;
                    }
                }
                if (cacheFound == false) {
                    dnsError();
                    addCacheEntryFailed(StatusEntry_Timeout, 30, query.host);
                }
                query.http.clear();
                query.https.clear();
            }

            unsigned int serverDNSindex = 0;
            while (serverDNSindex < dnsServerList.size()) {
                const DnsServerEntry &dnsServer =
                    dnsServerList.at(serverDNSindex);
                if (dnsServer.mode == Mode_IPv6) {
                    const int result = sendto(
                        IPv6Socket, query.query.data(), query.query.size(), 0,
                        (struct sockaddr *)&dnsServer.targetDnsIPv6,
                        sizeof(dnsServer.targetDnsIPv6));
                    if (result != (int)query.query.size()) {
                    } else {
                        sendOk = true;
                    }
                } else  // if(mode==Mode_IPv4)
                {
                    const int result = sendto(
                        IPv4Socket, query.query.data(), query.query.size(), 0,
                        (struct sockaddr *)&dnsServer.targetDnsIPv4,
                        sizeof(dnsServer.targetDnsIPv4));
                    if (result != (int)query.query.size()) {
                    } else {
                        sendOk = true;
                    }
                }
                serverDNSindex++;
            }

            if (query.retryTime >= DnsSeeker::retryBeforeError() || !sendOk) {
                const std::vector<int> &http = query.http;
                const std::vector<int> &https = query.https;
                bool cacheFound = false;
                if (cacheAAAA.find(query.host) != cacheAAAA.cend()) {
                    CacheAAAAEntry &entry = cacheAAAA.at(query.host);
                    uint64_t t = time(NULL);
                    const uint64_t &maxTime = t + 24 * 3600;
                    // fix time drift
                    if (entry.outdated_date > maxTime)
                        entry.outdated_date = maxTime;
                    if (entry.status == StatusEntry_Right) {
                        if (!https.empty()) {
                            memcpy(&targetHttps.sin6_addr, &entry.sin6_addr,
                                   16);
                            dnsRight(targetHttps, true);
                        }
                        if (!http.empty()) {
                            memcpy(&targetHttp.sin6_addr, &entry.sin6_addr, 16);
                            dnsRight(targetHttp, false);
                        }
                        cacheFound = true;
                    }
                }
                if (cacheFound == false) {
                    dnsError();
                    addCacheEntryFailed(StatusEntry_Timeout, 30, query.host);
                }
                removeQuery(id);
            } else {
                query.nextRetry = msFrom1970() + resendQueryDNS_ms();
                this->queryByNextDueTime[query.nextRetry].push_back(id);
            }
        }
        this->queryByNextDueTime.erase(t);
    }
    for (const auto &n : queryList) {
        const uint16_t &queryId = n.first;
        Query &query = queryList[queryId];

        if (query.retryTime >= DnsSeeker::retryBeforeError()) {
            const std::vector<int> &http = query.http;
            const std::vector<int> &https = query.https;
            bool cacheFound = false;
            if (cacheAAAA.find(query.host) != cacheAAAA.cend()) {
                CacheAAAAEntry &entry = cacheAAAA.at(query.host);
                uint64_t t = time(NULL);
                const uint64_t &maxTime = t + 24 * 3600;
                // fix time drift
                if (entry.outdated_date > maxTime)
                    entry.outdated_date = maxTime;
                if (entry.status == StatusEntry_Right) {
                    if (!https.empty()) {
                        memcpy(&targetHttps.sin6_addr, &entry.sin6_addr, 16);
                        dnsRight(targetHttps, true);
                    }
                    if (!http.empty()) {
                        memcpy(&targetHttp.sin6_addr, &entry.sin6_addr, 16);
                        dnsRight(targetHttp, false);
                    }
                    cacheFound = true;
                }
            }
            if (cacheFound == false) {
                dnsError();
                addCacheEntryFailed(StatusEntry_Timeout, 30, query.host);
            }
            removeQuery(queryId);
        }
    }
}

uint64_t DnsSeeker::msFrom1970()  // ms from 1970
{
    struct timeval te;
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000LL + te.tv_usec / 1000;
}

in6_addr *DnsSeeker::GetEntry(const std::string &host) {
    if (cacheAAAA.find(host) != cacheAAAA.cend()) {
        CacheAAAAEntry &entry = cacheAAAA.at(host);
        return &entry.sin6_addr;
    }
    return nullptr;
}
