#include "Http.hpp"

#pragma GCC diagnostic ignored "-Wpointer-arith"

#include <arpa/inet.h>
#include <string.h>

#include <sstream>
#include <vector>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "Settings.hpp"

#define ZERO_LENGTH 10

Http::Http() {
   buffer_size_ = Settings::HttpBufferSize;

   one_ = malloc(1);
   memset(one_, 1, 1);

   zero_ = malloc(ZERO_LENGTH);
   memset(zero_, 0, ZERO_LENGTH);
}

Http::~Http() {
   free(one_);
   free(zero_);
}

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::SetCache(Cache *cache) { cache_ = cache; }

void Http::AddFetchDataRequest(struct Request *req) {}

int Http::GetDataReadedLength(void *src, void *is_header) {
   // TODO(Javier Garson): Optimize with a algorithm like bubble sort
   // PERFORMANCE LEAK
   int offset = 0;
   if (memcmp(is_header, one_, 1) == 0) {
      char *tmp = (char *)src;
      while (offset < buffer_size_) {
         if (tmp[offset] == '\r' && tmp[offset + 1] == '\n') {
            memset(is_header, 0, 1);
            offset++;
            break;
         }
         offset++;
      }
   }
   while (offset < buffer_size_) {
      if (memcmp(src + offset, zero_, ZERO_LENGTH) == 0) {
         return offset;
      }
      offset++;
   }
   return buffer_size_;
}

struct Request *Http::UnifyBuffer(struct Request *request) {
   struct HttpRequest *http_request = waiting_read_.at(request->client_socket);
   struct Request *auxiliar = http_request->request;
   std::vector<struct iovec> buffer = http_request->buffer;
   int size = http_request->size;

   auxiliar->iov[3].iov_base = malloc(size);
   auxiliar->iov[3].iov_len = size;

   size_t offset = 0;
   for (auto it = begin(buffer); it != end(buffer); ++it) {
      memcpy(auxiliar->iov[3].iov_base + offset, it->iov_base, it->iov_len);
      offset += it->iov_len;
   }

   return auxiliar;
}
