#include "Http.hpp"

#include <arpa/inet.h>
#include <string.h>

#include <sstream>
#include <vector>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

Http::Http() {
   buffer_size_ = 1024;
}

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::SetServer(Server *server) { server_ = server; }

void Http::SetCache(Cache *cache) { cache_ = cache; }

void Http::AddFetchDataRequest(struct Request *req) {}

void Http::AddReadRequest(struct Request *request, int fd) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);

   struct HttpRequest *http_request = new HttpRequest();
   http_request->request = request;
   http_request->size = 0;
   std::pair<int, struct HttpRequest *> item(fd, http_request);
   waiting_read_.insert(item);

   struct Request *auxiliar = Utils::CreateRequest(1);
   auxiliar->event_type = EVENT_TYPE_HTTP_READ;
   auxiliar->client_socket = fd;
   auxiliar->iov[0].iov_base = malloc(buffer_size_);
   auxiliar->iov[0].iov_len = buffer_size_;
   memset(auxiliar->iov[0].iov_base, 0, buffer_size_);

   io_uring_prep_readv(sqe, fd, &auxiliar->iov[0], 1, 0);
   io_uring_sqe_set_data(sqe, auxiliar);
   io_uring_submit(ring_);
}

int Http::GetDataReadedLength(char *src) {
   // TODO(Javier Garson): Optimize with a algorithm like bubble sort
   // PERFORMANCE LEAK
   for (int i = 0; i < buffer_size_; i++) {
      if (src[i] == '\0') {
         if (i < buffer_size_ - 1 && src[i + 1] == '\0') {
            return i;
         }
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

void Http::ReleaseSocket(struct Request *request) {
   struct HttpRequest *http_request = waiting_read_.at(request->client_socket);
   close(request->client_socket);
   waiting_read_.erase(request->client_socket);

   for (auto it = begin(http_request->buffer); it != end(http_request->buffer); ++it) {
      free(it->iov_base);
   }

   delete http_request;
}
