#include "HttpClient.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <sstream>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

HttpClient::HttpClient() : Http() {}

int HttpClient::HandleFetchData(struct Request *request, bool ipv4) {
   std::string url((char *)request->iov[0].iov_base);
   url = url.substr(1);
   std::size_t pos = url.find("/");
   std::string host = url.substr(0, pos);
   std::string query = url.substr(pos);

   Log(__FILE__, __LINE__) << query;

   int sock = -1;
   int is_connected = -1;

   if (ipv4) {
      struct sockaddr_in *client =
          (struct sockaddr_in *)request->iov[4].iov_base;
      sock = socket(AF_INET, SOCK_STREAM, 0);
      if (sock > 0) {
         is_connected = connect(sock, (struct sockaddr *)client, sizeof(struct sockaddr_in));
      }
   } else {
      struct sockaddr_in6 *client =
          (struct sockaddr_in6 *)request->iov[4].iov_base;
      sock = socket(AF_INET6, SOCK_STREAM, 0);
      if (sock > 0) {
         is_connected = connect(sock, (struct sockaddr *)client, sizeof(struct sockaddr_in6));
      }
   }

   if (sock < 0) {
      Log(__FILE__, __LINE__, Log::kError) << "Error creating socket";

      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      Utils::ReleaseRequest(request);
      return 1;
   }

   if (is_connected < 0) {
      close(sock);
      Log(__FILE__, __LINE__, Log::kError) << "Could not connect ";
      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      Utils::ReleaseRequest(request);
      return 1;
   }

   std::stringstream ss;
   ss << "GET " << query << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << (char*)request->iov[5].iov_base;
   std::string request_data = ss.str();

   if (send(sock, request_data.c_str(), request_data.length(), 0) !=
       (int)request_data.length()) {
      Log(__FILE__, __LINE__, Log::kError) << "invalid socket";
      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      Utils::ReleaseRequest(request);
      return 1;
   }

   char *path = (char *)request->iov[1].iov_base;
   int cache_socket = open(path, O_CREAT | O_WRONLY | O_TRUNC,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
   if (cache_socket < 0) {
      Log(__FILE__, __LINE__, Log::kError)
          << "wrong write file " << path << " " << strerror(errno);
      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      return 1;
   }

   AddReadRequest(request, sock, cache_socket);
   return 0;
}

void HttpClient::AddReadRequest(struct Request *request, int fd, int cache_socket) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);

   struct HttpRequest *http_request = new HttpRequest();
   http_request->has_header = 0;
   std::pair<uint64_t, struct HttpRequest *> item(request->resource_id, http_request);
   waiting_read_.insert(item);

   struct Request *auxiliar = Utils::HttpExternalRequest();
   auxiliar->event_type = EVENT_TYPE_HTTP_READ;
   auxiliar->client_socket = fd;
   auxiliar->cache_socket = cache_socket;
   auxiliar->resource_id = request->resource_id;
   auxiliar->iov[0].iov_base = malloc(buffer_size_);
   auxiliar->iov[0].iov_len = buffer_size_;
   memset(auxiliar->iov[0].iov_base, 0, buffer_size_);

   io_uring_prep_readv(sqe, fd, &auxiliar->iov[0], 1, 0);
   io_uring_sqe_set_data(sqe, auxiliar);
   io_uring_submit(ring_);
}

int HttpClient::HandleReadData(struct Request *request, int readed) {
   struct HttpRequest *http_request = waiting_read_.at(request->resource_id);

   if (readed <= 0) {
      cache_->CloseStream(request->resource_id);
      ReleaseSocket(request);
      return 1;
   }

   if (http_request->has_header == 0) {
      int header_size = FetchHeader(request->iov[0].iov_base);
      void *header = malloc(header_size);
      memcpy(header, request->iov[0].iov_base, header_size);
      cache_->SetHeaderRequest(request->resource_id, header, header_size);
      http_request->has_header = 1;
   }

   // If there is no listeners
   if (cache_->AddWriteRequestStream(request->resource_id, request->iov[0].iov_base, readed) == 1) {
      Log(__FILE__, __LINE__) << "HttpClient empty stream listeners";
      ReleaseSocket(request);
      return 1;
   }

   memset(request->iov[0].iov_base, 0, buffer_size_);

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_readv(sqe, request->client_socket, &request->iov[0], 1, 0);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);

   return 0;
}

void HttpClient::ReleaseSocket(struct Request *inner_request) {
   struct HttpRequest *http_request = waiting_read_.at(inner_request->resource_id);
   close(inner_request->client_socket);
   waiting_read_.erase(inner_request->resource_id);
   Utils::ReleaseRequest(inner_request);

   delete http_request;
}
