#include "Http.hpp"

#pragma GCC diagnostic ignored "-Wpointer-arith"

#include <arpa/inet.h>
#include <string.h>

#include <sstream>
#include <vector>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

Http::Http() { buffer_size_ = 102400; }

Http::~Http() {}

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::SetServer(Server *server) { server_ = server; }

void Http::SetCache(Cache *cache) { cache_ = cache; }

void Http::AddFetchDataRequest(struct Request *req) {}

int Http::GetDataReadedLength(char *src) {
   // TODO(Javier Garson): Optimize with a algorithm like bubble sort
   // PERFORMANCE LEAK
   for (int i = 0; i < buffer_size_ - 1; i++) {
      if (src[i] == '\0' && src[i + 1] == '\0') {
         return i;
      }
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
