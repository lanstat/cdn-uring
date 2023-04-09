#include "Http.hpp"

#include <arpa/inet.h>
#include <string.h>

#include <algorithm>
#include <sstream>
#include <vector>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"

#define ZERO_LENGTH 1

Http::Http() {
   stream_ = nullptr;
   cache_ = nullptr;

   buffer_size_ = Settings::HttpBufferSize;

   zero_ = malloc(ZERO_LENGTH);
   memset(zero_, 0, ZERO_LENGTH);
}

Http::~Http() { free(zero_); }

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::SetCache(Cache *cache) { cache_ = cache; }

void Http::SetStream(Stream *stream) { stream_ = stream; }

void Http::AddFetchDataRequest(struct Request *req) {}

int Http::GetResourceType(char *header, int size) {
   return Settings::HLSMode ? RESOURCE_TYPE_STREAMING : RESOURCE_TYPE_CACHE;
}

int Http::FetchHeaderLength(char *header, int size) {
   int offset = 0;
   while (offset < size) {
      if (header[offset] == '\r' && header[offset + 1] == '\n' &&
          header[offset + 2] == '\r' && header[offset + 3] == '\n') {
         offset += 4;
         break;
      }
      offset++;
   }
   return offset;
}

template <typename charT>
struct my_equal {
   my_equal(const std::locale &loc) : loc_(loc) {}
   bool operator()(charT ch1, charT ch2) {
      return std::toupper(ch1, loc_) == std::toupper(ch2, loc_);
   }

  private:
   const std::locale &loc_;
};

template <typename T>
int ci_find_substr(const T &str1, const T &str2,
                   const std::locale &loc = std::locale()) {
   typename T::const_iterator it =
       std::search(str1.begin(), str1.end(), str2.begin(), str2.end(),
                   my_equal<typename T::value_type>(loc));
   if (it != str1.end()) {
      return it - str1.begin();
   } else {
      return -1;  // not found
   }
}

std::string Http::GetExternalHeader(char *header) {
   std::string tmp(header);
   std::string test = "\r\nConnection:";

   int pos = ci_find_substr(tmp, test);
   if (pos < 0) {
      tmp = tmp + "Connection: close\r\n";
   } else {
      std::string first = tmp.substr(0, pos);
      std::string second = tmp.substr(pos + 2);
      tmp = first + "\r\nConnection: close" + second.substr(second.find("\r\n"));
   }
   return tmp + "\r\n";
}
