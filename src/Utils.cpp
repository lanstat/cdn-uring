#include "Utils.hpp"

#include <algorithm>
#include <climits>
#include <cstring>

#include "Settings.hpp"

/*
 * Utility function to convert a string to lower case.
 * */
void Utils::StrToLower(char *str) {
   for (; *str; ++str) *str = (char)tolower(*str);
}

/*
 * Helper function for cleaner looking code.
 * */
void *Utils::ZhMalloc(size_t size) {
   void *buf = malloc(size);
   if (!buf) {
      fprintf(stderr, "Fatal error: unable to allocate memory.\n");
      exit(1);
   }
   return buf;
}

struct Request *Utils::CreateRequest(int iovec_count) {
   struct Request *req =
       (Request *)malloc(sizeof(*req) + sizeof(struct iovec) * iovec_count);
   req->iovec_count = iovec_count;
   for (int i = 0; i < req->iovec_count; i++) {
      req->iov[i].iov_len = 0;
   }
   req->pivot = 0;
   req->is_processing = false;
   req->debug = false;
   return req;
}

/*
 * Object for entry http request
 *
 * 0 = char* request's header
 * 1 = resource http path
 * 2 = header content
 */
struct Request *Utils::HttpEntryRequest() { return CreateRequest(3); }

/*
 * Object for http error request
 *
 * 0 = char* bytes with the error response
 */
struct Request *Utils::HttpErrorRequest() { return CreateRequest(1); }

/*
 * Object for http external request
 *
 * 0 = void* bytes bytes readed for the request
 */
struct Request *Utils::HttpExternalRequest(struct Request *cache) {
   struct Request *request = CreateRequest(1);
   request->resource_id = cache->resource_id;

   return request;
}

/*
 * Object for http secure external request
 *
 * 0 = void* bytes bytes readed for the request
 * 1 = SSL* ssl pointer
 * 2 = SSL_CTX* ssl context
 */
struct Request *Utils::HttpsExternalRequest(struct Request *inner) {
   struct Request *request = CreateRequest(3);
   request->resource_id = inner->resource_id;

   return request;
}

/*
 * Object for stream request
 *
 * 0 = void* bytes to r/w to client socket
 */
struct Request *Utils::StreamRequest(struct Request *entry) {
   struct Request *request = CreateRequest(1);
   request->resource_id = entry->resource_id;
   request->client_socket = entry->client_socket;

   return request;
}

/*
 * Object for cache request
 *
 * 0 = statx* status of the resource in cache
 * 1 = char* guid for the cache resource
 * 2 = char* path requested by the client eg. google.com/images
 * 3 = char* client header
 * 4 = sockaddr_in|sockaddr_in6 * pointer to external server
 */
struct Request *Utils::InnerRequest(struct Request *entry) {
   struct Request *request = CreateRequest(5);
   request->resource_id = entry->resource_id;

   return request;
}

/*
 * Object for stream request
 *
 * 0 = void* bytes to w to client socket
 */
struct Request *Utils::CacheRequest(struct Request *entry) {
   struct Request *request = CreateRequest(1);
   request->resource_id = entry->resource_id;
   request->iov[0].iov_base = malloc(Settings::HttpBufferSize);
   request->iov[0].iov_len = Settings::HttpBufferSize;
   memset(request->iov[0].iov_base, 0, request->iov[0].iov_len);

   return request;
}

void Utils::ReleaseRequest(struct Request *request) {
   for (int i = 0; i < request->iovec_count; i++) {
      if (request->iov[i].iov_len > 0) {
         free(request->iov[i].iov_base);
      }
   }
   free(request);
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

std::string Utils::ReplaceHeaderTag(std::string header,
                                    const std::string &to_search,
                                    const std::string &replaced) {
   std::string tmp = "\r\n" + to_search + ":";
   int pos = ci_find_substr(header, tmp);
   if (pos < 0) {
      header = header + tmp + " " + replaced + "\r\n";
   } else {
      std::string first = header.substr(0, pos);
      std::string second = header.substr(pos + 2);
      header =
          first + tmp + " " + replaced + second.substr(second.find("\r\n"));
   }
   return header;
}

std::string Utils::GetHeaderTag(std::string header,
                                const std::string &to_search) {
   std::string tmp = "\r\n" + to_search + ":";
   int pos = ci_find_substr(header, tmp);
   if (pos < 0) {
      return std::string();
   } else {
      std::string first = header.substr(0, pos);
      std::string second = header.substr(pos + 4 + to_search.size());
      return second.substr(0, second.find("\r\n"));
   }
}
