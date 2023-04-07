#include "HttpClient.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <sstream>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

HttpClient::HttpClient() : Http() {}

bool HttpClient::HandleFetchRequest(struct Request *inner, bool ipv4) {
   std::string url((char *)inner->iov[2].iov_base);
   url = url.substr(1);
   std::size_t pos = url.find("/");
   std::string host = url.substr(0, pos);
   std::string query = url.substr(pos);

   Log(__FILE__, __LINE__) << query;

   int sock = -1;
   int is_connected = -1;

   if (ipv4) {
      struct sockaddr_in *client = (struct sockaddr_in *)inner->iov[4].iov_base;
      sock = socket(AF_INET, SOCK_STREAM, 0);
      if (sock > 0) {
         is_connected = connect(sock, (struct sockaddr *)client,
                                sizeof(struct sockaddr_in));
      }
   } else {
      struct sockaddr_in6 *client =
          (struct sockaddr_in6 *)inner->iov[4].iov_base;
      sock = socket(AF_INET6, SOCK_STREAM, 0);
      if (sock > 0) {
         is_connected = connect(sock, (struct sockaddr *)client,
                                sizeof(struct sockaddr_in6));
      }
   }

   if (sock < 0) {
      Log(__FILE__, __LINE__, Log::kError) << "Error creating socket";

      stream_->ReleaseErrorAllWaitingRequest(inner->resource_id, 502);
      return false;
   }

   if (is_connected < 0) {
      close(sock);
      Log(__FILE__, __LINE__, Log::kError) << "Could not connect ";
      stream_->ReleaseErrorAllWaitingRequest(inner->resource_id, 502);
      return false;
   }

   std::stringstream ss;
   ss << "GET " << query << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << (char *)inner->iov[3].iov_base;
   std::string request_data = ss.str();

   if (send(sock, request_data.c_str(), request_data.length(), 0) !=
       (int)request_data.length()) {
      Log(__FILE__, __LINE__, Log::kError) << "invalid socket";
      stream_->ReleaseErrorAllWaitingRequest(inner->resource_id, 502);
      return false;
   }

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);

   struct Request *http = Utils::HttpExternalRequest(inner);
   http->event_type = EVENT_TYPE_HTTP_READ_HEADER;
   http->client_socket = sock;
   http->pivot = RESOURCE_TYPE_UNDEFINED;
   http->iov[0].iov_base = malloc(buffer_size_);
   http->iov[0].iov_len = buffer_size_;
   memset(http->iov[0].iov_base, 0, buffer_size_);

   // io_uring_prep_readv(sqe, sock, &http->iov[0], 1, 0);
   io_uring_prep_read(sqe, http->client_socket, http->iov[0].iov_base,
                      buffer_size_, 0);
   io_uring_sqe_set_data(sqe, http);
   io_uring_submit(ring_);
   return true;
}

int HttpClient::HandleReadHeaderRequest(struct Request *http, int readed) {
   if (readed <= 0) {
      stream_->CloseStream(http->resource_id);
      ReleaseSocket(http);
      return 1;
   }

   int type = GetResourceType((char *)http->iov[0].iov_base, readed);

   http->iov[0].iov_len = readed;

   if (type == RESOURCE_TYPE_CACHE) {
      cache_->GenerateNode(http);
   } else if (type == RESOURCE_TYPE_STREAMING) {
      stream_->SetStreamingResource(http->resource_id, http);
   }

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   http->event_type = EVENT_TYPE_HTTP_READ_CONTENT;
   http->pivot = type;
   io_uring_prep_read(sqe, http->client_socket, http->iov[0].iov_base,
                      buffer_size_, 0);
   io_uring_sqe_set_data(sqe, http);
   io_uring_submit(ring_);

   return 0;
}

int HttpClient::HandleReadData(struct Request *http, int readed) {
   int type = http->pivot;
   if (readed <= 0) {
      if (type == RESOURCE_TYPE_CACHE) {
         cache_->CloseBuffer(http->resource_id);
      } else if (type == RESOURCE_TYPE_STREAMING) {
         stream_->CloseStream(http->resource_id);
      }
      ReleaseSocket(http);
      return 1;
   }

   if (type == RESOURCE_TYPE_CACHE) {
      cache_->AppendBuffer(http->resource_id, http->iov[0].iov_base, readed);
   } else if (type == RESOURCE_TYPE_STREAMING) {
      // If there is no listeners
      if (stream_->NotifyStream(http->resource_id, http->iov[0].iov_base,
                                readed) == 1) {
         Log(__FILE__, __LINE__) << "HttpClient empty stream listeners";
         ReleaseSocket(http);
         return 1;
      }
   }

   memset(http->iov[0].iov_base, 0, buffer_size_);

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_read(sqe, http->client_socket, http->iov[0].iov_base,
                      buffer_size_, 0);
   io_uring_sqe_set_data(sqe, http);
   io_uring_submit(ring_);

   return 0;
}

void HttpClient::ReleaseSocket(struct Request *http) {
   close(http->client_socket);
   Utils::ReleaseRequest(http);
}
