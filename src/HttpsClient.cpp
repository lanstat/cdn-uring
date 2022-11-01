#include "HttpsClient.hpp"

#include <arpa/inet.h>

#include <sstream>
#include <cstring>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

HttpsClient::HttpsClient() : Http() {}

int HttpsClient::HandleFetchData(struct Request *request) {
   std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< "cool" << std::endl;
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
      return 1;
   }

   if (connect(sock, (struct sockaddr *)client, sizeof(struct sockaddr_in6)) <
       0) {
      close(sock);
      Log(__FILE__, __LINE__, Log::kError) << "Could not connect ";
      server_->AddHttpErrorRequest(request->client_socket, 502);
      Utils::ReleaseRequest(request);
      return 1;
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
      return 1;
   }

   AddReadRequest(request, sock);
   return 0;
}

int HttpsClient::HandleReadData(struct Request *request) {
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

   memset(request->iov[0].iov_base, 0, buffer_size_);

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_readv(sqe, request->client_socket, &request->iov[0], 1, 0);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);

   return 0;
}
