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

bool HttpsClient::HandleFetchRequest(struct Request *request, bool ipv4) {
   /*
   std::string url((char *)request->iov[0].iov_base);
   url = url.substr(1);
   std::size_t pos = url.find("/");
   std::string host = url.substr(0, pos);
   std::string query = url.substr(pos);

   Log(__FILE__, __LINE__) << query;

   struct sockaddr_in6 *client =
       (struct sockaddr_in6 *)request->iov[4].iov_base;

   const SSL_METHOD *method = TLS_client_method();
   SSL_CTX *ctx = SSL_CTX_new(method);
   if (!ctx) {
      Log(__FILE__, __LINE__, Log::kError) << "Error creating context ssl";
      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      return 1;
   }

   SSL *ssl = SSL_new(ctx);
   if (!ssl) {
      CloseSSL(-1, nullptr, ctx);
      Log(__FILE__, __LINE__, Log::kError) << "Error creating ssl";
      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      Utils::ReleaseRequest(request);
      return -1;
   }

   int socket_fd = socket(AF_INET6, SOCK_STREAM, 0);
   if (socket_fd < 0) {
      CloseSSL(-1, ssl, ctx);
      Log(__FILE__, __LINE__, Log::kError) << "Error creating socket";
      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      Utils::ReleaseRequest(request);
      return 1;
   }

   if (connect(socket_fd, (struct sockaddr *)client,
               sizeof(struct sockaddr_in6)) < 0) {
      CloseSSL(socket_fd, ssl, ctx);
      Log(__FILE__, __LINE__, Log::kError) << "Could not connect ";
      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      Utils::ReleaseRequest(request);
      return 1;
   }

   SSL_set_fd(ssl, socket_fd);
   int err = SSL_connect(ssl);
   if (err < 1) {
      CloseSSL(socket_fd, ssl, ctx);
      Log(__FILE__, __LINE__, Log::kError) << "Could not connect ";
      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      Utils::ReleaseRequest(request);
      return 1;
   }

   std::stringstream ss;
   ss << "GET " << query << " HTTP/1.2\r\n"
      << "Host: " << host << "\r\n"
      << "Accept: *//*\r\n"
      << "Connection: close\r\n"
      << "\r\n\r\n";
   std::string request_data = ss.str();

   if (SSL_write(ssl, request_data.c_str(), request_data.length()) < 0) {
      CloseSSL(socket_fd, ssl, ctx);
      Log(__FILE__, __LINE__, Log::kError) << "invalid socket";
      cache_->ReleaseErrorAllWaitingRequest(request, 502);
      Utils::ReleaseRequest(request);
      return 1;
   }

   AddReadRequest(request, ssl, ctx, socket_fd);
   return 0;
   */
   return false;
}

void HttpsClient::AddReadRequest(struct Request *request, SSL *ssl,
                                 SSL_CTX *context, int fd) {
   // struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);

   // struct HttpRequest *http_request = new HttpRequest();
   // http_request->has_header = 0;
   // std::pair<int, struct HttpRequest *> item(fd, http_request);
   // waiting_read_.insert(item);

   // struct Request *auxiliar = Utils::HttpsExternalRequest();
   // auxiliar->event_type = EVENT_TYPE_HTTP_READ;
   // auxiliar->client_socket = fd;
   // auxiliar->iov[0].iov_base = malloc(buffer_size_);
   // auxiliar->iov[0].iov_len = buffer_size_;
   // auxiliar->iov[1].iov_base = ssl;
   // auxiliar->iov[2].iov_base = context;

   // memset(auxiliar->iov[0].iov_base, 0, buffer_size_);

   // io_uring_prep_nop(sqe);
   // io_uring_sqe_set_data(sqe, auxiliar);
   // io_uring_submit(ring_);
}

int HttpsClient::HandleReadData(struct Request *request, int response) {
   // SSL *ssl = (SSL *)request->iov[1].iov_base;
   // int err = SSL_read(ssl, request->iov[0].iov_base, buffer_size_);
   // if (err < 1) {
   // if (!ProcessError(ssl, err)) {
   // struct HttpRequest *http_request =
   // waiting_read_.at(request->client_socket);
   ////TODO(lanstat): verify when is finished
   ////cache_->ReleaseErrorAllWaitingRequest(http_request->request, 502);
   // ReleaseSocket(request);
   // return 1;
   //}
   //}

   // struct HttpRequest *http_request =
   // waiting_read_.at(request->client_socket); int readed =
   // FetchHeader(request->iov[0].iov_base); Log(__FILE__, __LINE__,
   // Log::kDebug) << "bytes readed: " << readed; if (readed <= 0) { struct
   // Request *client_request = UnifyBuffer(request); ReleaseSocket(request);
   // cache_->AddWriteRequest(client_request);
   // return 1;
   //} else {
   // struct iovec data;
   // data.iov_base = malloc(readed);
   // data.iov_len = readed;
   // memcpy(data.iov_base, request->iov[0].iov_base, readed);
   //}

   // memset(request->iov[0].iov_base, 0, buffer_size_);

   // struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   // io_uring_prep_nop(sqe);
   // io_uring_sqe_set_data(sqe, request);
   // io_uring_submit(ring_);

   return 0;
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
   // struct HttpRequest *http_request =
   // waiting_read_.at(request->client_socket);
   // waiting_read_.erase(request->client_socket);

   // free(request->iov[0].iov_base);
   // SSL *ssl = (SSL *)request->iov[1].iov_base;
   // SSL_CTX *context = (SSL_CTX *)request->iov[2].iov_base;

   // CloseSSL(request->client_socket, ssl, context);

   // delete http_request;
   // free(request);
}

bool HttpsClient::ProcessError(SSL *ssl, int last_error) {
   int error = SSL_get_error(ssl, last_error);
   Log(__FILE__, __LINE__, Log::kWarning) << "SSL error " << error;
   if (error == SSL_ERROR_NONE) {
      // if (last_error == SSL_ERROR_SYSCALL) {
      // return false;
      //}
      return true;
   } else if (error == SSL_ERROR_ZERO_RETURN) {
      SSL_shutdown(ssl);
   } else if (error == SSL_ERROR_SYSCALL) {
      return ProcessError(ssl, error);
   }
   return false;
}

int HttpsClient::HandleReadHeaderRequest(struct Request *http, int readed) {
   return 0;
}
