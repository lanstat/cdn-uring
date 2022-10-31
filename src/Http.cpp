#include "Http.hpp"

#include <arpa/inet.h>
#include <string.h>

#include <sstream>
#include <vector>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#define BUFFER_SIZE 1024

Http::Http() {}

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::SetServer(Server *server) { server_ = server; }

void Http::SetCache(Cache *cache) { cache_ = cache; }

void Http::AddFetchDataRequest(struct Request *req) {}

int Http::HandleFetchData(struct Request *request) {
   Fetch(request);
   return 0;
}

void Http::Fetch(struct Request *request) {
   std::string url((char *)request->iov[0].iov_base);
   url = url.substr(1);
   std::size_t pos = url.find("/");
   std::string host = url.substr(0, pos);
   std::string query = url.substr(pos);

   Log(__FILE__, __LINE__) << query;

   struct sockaddr_in6 *client =
       (struct sockaddr_in6 *)request->iov[4].iov_base;
   int sock = socket(AF_INET6, SOCK_STREAM, 0);

   if (sock < 0) {
      Log(__FILE__, __LINE__, Log::kError) << "Error creating socket";

      server_->AddHttpErrorRequest(request->client_socket, 502);
      Utils::ReleaseRequest(request);
      return;
   }

   if (connect(sock, (struct sockaddr *)client, sizeof(struct sockaddr_in6)) <
       0) {
      close(sock);
      Log(__FILE__, __LINE__, Log::kError) << "Could not connect ";
      server_->AddHttpErrorRequest(request->client_socket, 502);
      Utils::ReleaseRequest(request);
      return;
   }

   std::stringstream ss;
   ss << "GET " << query << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << "Accept: */*\r\n"
      << "Connection: close\r\n"
      << "\r\n\r\n";
   std::string request_data = ss.str();

   if (send(sock, request_data.c_str(), request_data.length(), 0) !=
       (int)request_data.length()) {
      Log(__FILE__, __LINE__, Log::kError) << "invalid socket";
      server_->AddHttpErrorRequest(request->client_socket, 502);
      Utils::ReleaseRequest(request);
      return;
   }

   AddReadRequest(request, sock);
   /*
   std::vector<unsigned char> buffer;
   unsigned char tmp[BUFFER_SIZE];
   int returned;
   while ((returned = read(sock, &tmp, BUFFER_SIZE)) > 0) {
      int pivot = returned / sizeof(tmp[0]);
      buffer.insert(buffer.end(), tmp, tmp + pivot);
   }
   close(sock);

   request->iov[3].iov_base = malloc(buffer.size());
   request->iov[3].iov_len = buffer.size();

   memcpy(request->iov[3].iov_base, buffer.data(), buffer.size());
   */
}

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
   auxiliar->iov[0].iov_base = malloc(BUFFER_SIZE);
   auxiliar->iov[0].iov_len = BUFFER_SIZE;
   memset(auxiliar->iov[0].iov_base, 0, BUFFER_SIZE);

   io_uring_prep_readv(sqe, fd, &auxiliar->iov[0], 1, 0);
   io_uring_sqe_set_data(sqe, auxiliar);
   io_uring_submit(ring_);
}

int Http::HandleReadData(struct Request *request) {
   int readed = GetDataReadedLength((char *)request->iov[0].iov_base);

   if (readed <= 0) {
      struct Request *client_request = UnifyBuffer(request);
      ReleaseSocket(request);
      cache_->AddWriteRequest(client_request);
      return 1;
   } else {
      struct HttpRequest *http_request =
          waiting_read_.at(request->client_socket);
      struct iovec data;
      data.iov_base = malloc(readed);
      data.iov_len = readed;
      memcpy(data.iov_base, request->iov[0].iov_base, readed);
      http_request->buffer.push_back(data);
      http_request->size += readed;
   }

   memset(request->iov[0].iov_base, 0, BUFFER_SIZE);

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_readv(sqe, request->client_socket, &request->iov[0], 1, 0);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);

   return 0;
}

int Http::GetDataReadedLength(char *src) {
   // TODO(Javier Garson): Optimize with a algorithm like bubble sort
   // PERFORMANCE LEAK
   for (int i = 0; i < BUFFER_SIZE; i++) {
      if (src[i] == '\0') {
         if (i < BUFFER_SIZE - 1 && src[i + 1] == '\0') {
            return i;
         }
         return i;
      }
   }
   return BUFFER_SIZE;
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
