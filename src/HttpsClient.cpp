#include "HttpsClient.hpp"

#include <arpa/inet.h>
#include <openssl/err.h>
#include <unistd.h>

#include <cstring>
#include <sstream>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

HttpsClient::HttpsClient() : Http() {
   OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
}

bool HttpsClient::HandleFetchRequest(struct Request *inner, bool ipv4) {
   std::string url((char *)inner->iov[2].iov_base);
   url = url.substr(1);
   std::size_t pos = url.find("/");
   std::string host = url.substr(0, pos);
   std::string query = url.substr(pos);

   Log(__FILE__, __LINE__) << query;

   const SSL_METHOD *method = TLS_client_method();
   SSL_CTX *ctx = SSL_CTX_new(method);
   if (!ctx) {
      Log(__FILE__, __LINE__, Log::kError) << "Error creating context ssl";
      stream_->ReleaseErrorAllWaitingRequest(inner->resource_id, 502);
      return false;
   }

   SSL *ssl = SSL_new(ctx);
   if (!ssl) {
      CloseSSL(-1, nullptr, ctx);
      Log(__FILE__, __LINE__, Log::kError) << "Error creating ssl";
      stream_->ReleaseErrorAllWaitingRequest(inner->resource_id, 502);
      return false;
   }

   int socket_fd = -1;
   int is_connected = -1;

   if (ipv4) {
      struct sockaddr_in *client = (struct sockaddr_in *)inner->iov[4].iov_base;
      socket_fd = socket(AF_INET, SOCK_STREAM, 0);
      if (socket_fd > 0) {
         is_connected = connect(socket_fd, (struct sockaddr *)client,
                                sizeof(struct sockaddr_in));
      }
   } else {
      struct sockaddr_in6 *client =
          (struct sockaddr_in6 *)inner->iov[4].iov_base;
      socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
      if (socket_fd > 0) {
         is_connected = connect(socket_fd, (struct sockaddr *)client,
                                sizeof(struct sockaddr_in6));
      }
   }

   if (socket_fd < 0) {
      Log(__FILE__, __LINE__, Log::kError) << "Error creating socket";

      stream_->ReleaseErrorAllWaitingRequest(inner->resource_id, 502);
      return false;
   }

   if (is_connected < 0) {
      CloseSSL(socket_fd, ssl, ctx);
      Log(__FILE__, __LINE__, Log::kError) << "Could not connect ";
      stream_->ReleaseErrorAllWaitingRequest(inner->resource_id, 502);
      return false;
   }

   SSL_set_fd(ssl, socket_fd);
   int err = SSL_connect(ssl);
   if (err < 1) {
      CloseSSL(socket_fd, ssl, ctx);
      Log(__FILE__, __LINE__, Log::kError) << "Could not connect ";
      stream_->ReleaseErrorAllWaitingRequest(inner->resource_id, 502);
      return false;
   }

   std::stringstream ss;
   ss << "GET " << query << " HTTP/1.2\r\n"
      << "Host: " << host << "\r\n";
   std::string request_data =
       ss.str() + GetExternalHeader((char *)inner->iov[3].iov_base);

   if (SSL_write(ssl, request_data.c_str(), request_data.length()) < 0) {
      CloseSSL(socket_fd, ssl, ctx);
      Log(__FILE__, __LINE__, Log::kError) << "invalid socket";
      stream_->ReleaseErrorAllWaitingRequest(inner->resource_id, 502);
      return false;
   }

   struct Request *http = Utils::HttpsExternalRequest(inner);
   http->event_type = EVENT_TYPE_HTTP_READ_HEADER;
   http->client_socket = socket_fd;
   http->pivot = RESOURCE_TYPE_UNDEFINED;
   http->iov[0].iov_base = malloc(buffer_size_);
   http->iov[0].iov_len = buffer_size_;
   memset(http->iov[0].iov_base, 0, buffer_size_);
   http->iov[1].iov_base = ssl;
   http->iov[2].iov_base = ctx;

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   // io_uring_prep_read(sqe, http->client_socket, http->iov[0].iov_base,
   // buffer_size_, 0);
   io_uring_prep_nop(sqe);
   io_uring_sqe_set_data(sqe, http);
   io_uring_submit(ring_);
   return true;
}

void HttpsClient::CloseSSL(int socket_fd, SSL *ssl, SSL_CTX *context) {
   if (ssl != nullptr) {
      SSL_free(ssl);
   }

   if (socket_fd > 0) {
      close(socket_fd);
   }

   SSL_CTX_free(context);
}

void HttpsClient::ReleaseSocket(struct Request *request) {
   free(request->iov[0].iov_base);
   SSL *ssl = (SSL *)request->iov[1].iov_base;
   SSL_CTX *context = (SSL_CTX *)request->iov[2].iov_base;

   CloseSSL(request->client_socket, ssl, context);

   free(request);
}

bool HttpsClient::ProcessError(SSL *ssl, int last_error) {
   int error = SSL_get_error(ssl, last_error);
   if (error == SSL_ERROR_NONE) {
      // if (last_error == SSL_ERROR_SYSCALL) {
      // return false;
      //}
      return true;
   } else if (error == SSL_ERROR_ZERO_RETURN) {
      SSL_shutdown(ssl);
      return true;
   } else if (error == SSL_ERROR_SYSCALL) {
      Log(__FILE__, __LINE__, Log::kWarning) << "SSL error " << error;
      return ProcessError(ssl, error);
   }
   return false;
}

int HttpsClient::PreRequest(struct Request *http, int readed) {
   SSL *ssl = (SSL *)http->iov[1].iov_base;
   readed = SSL_read(ssl, http->iov[0].iov_base, buffer_size_);
   if (readed < 1) {
      if (!ProcessError(ssl, readed)) {
         return -1;
      }
      if (readed == 0) {
         return 0;
      }
   }
   return readed;
}

int HttpsClient::PostRequest(struct Request *http) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_nop(sqe);
   io_uring_sqe_set_data(sqe, http);
   io_uring_submit(ring_);
   return 0;
}
