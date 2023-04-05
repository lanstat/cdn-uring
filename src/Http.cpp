#include "Http.hpp"

#pragma GCC diagnostic ignored "-Wpointer-arith"

#include <arpa/inet.h>
#include <string.h>

#include <sstream>
#include <vector>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"

#define ZERO_LENGTH 20240

const char *end_line = "\r\n";
const char *ws = " \t\n\r\f\v";

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

int Http::FetchHeader(void *src) {
   int offset = 0;
   char *tmp = (char *)src;
   while (offset < buffer_size_) {
      if (tmp[offset] == '\r' && tmp[offset + 1] == '\n' &&
          tmp[offset + 2] == '\r' && tmp[offset + 3] == '\n') {
         return offset + 4;
      }
      offset++;
   }
   return 0;
}

struct Request *Http::UnifyBuffer(struct Request *request) {
   /*
   struct HttpRequest *http_request = waiting_read_.at(request->client_socket);
   struct Request *auxiliar = http_request->request;
   std::vector<struct iovec> buffer = http_request->buffer;
   int size = http_request->size;

   void *tmp = malloc(size);

   size_t offset = 0;
   for (auto it = begin(buffer); it != end(buffer); ++it) {
      memcpy(tmp + offset, it->iov_base, it->iov_len);
      offset += it->iov_len;
   }

   void *header = malloc(http_request->header_size);
   memcpy(header, tmp, http_request->header_size);
   std::string new_header = CreateHeader((char *)header);
   free(header);

   int header_size = sizeof(char) * new_header.length();
   int body_size = size - header_size;
   int new_size = header_size + body_size;
   auxiliar->iov[3].iov_base = malloc(new_size);
   auxiliar->iov[3].iov_len = new_size;

   memset(auxiliar->iov[3].iov_base, 0, new_size);
   memcpy(auxiliar->iov[3].iov_base, new_header.c_str(), header_size);
   memcpy(auxiliar->iov[3].iov_base + header_size, tmp + http_request->header_size, body_size);

   return auxiliar;
   */

   return nullptr;
}

inline std::string &rtrim(std::string &s, const char *t = ws) {
   s.erase(s.find_last_not_of(t) + 1);
   return s;
}

inline std::string &ltrim(std::string &s, const char *t = ws) {
   s.erase(0, s.find_first_not_of(t));
   return s;
}

inline std::string &trim(std::string &s, const char *t = ws) {
   return ltrim(rtrim(s, t), t);
}

int Http::GetResourceType(char *header, int size) {
   std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< header << std::endl;
   return RESOURCE_TYPE_STREAMING;
}

std::string Http::CreateHeader(char *prev_header) {
   std::stringstream ss(prev_header);
   std::string line;
   std::string tmp;
   std::unordered_map<std::string, std::string> header_data;

   while (std::getline(ss, line, '\n')) {
      auto pivot = line.find(":");
      if (pivot != std::string::npos) {
         std::string value = line.substr(pivot + 1);
         std::pair<std::string, std::string> item(line.substr(0, pivot),
                                                  trim(value));
         header_data.insert(item);
      }
   }

   tmp.append("HTTP/1.2 200 OK");
   tmp.append(end_line);

   std::string keys[8] = {"Content-Type",  "Content-Length", "Date",
                          "Last-Modified", "Connection",     "Expires",
                          "Cache-Control", "Accept-Ranges"};

   for (int i = 0; i < 8; i++) {
      tmp.append(keys[i]);
      tmp.append(": ");
      tmp.append(header_data[keys[i]]);
      tmp.append(end_line);
   }
   tmp.append("Server: nginx");
   tmp.append(end_line);
   tmp.append("Server-Key: uring");
   tmp.append(end_line);

   tmp.append(end_line);

   return tmp;
}
