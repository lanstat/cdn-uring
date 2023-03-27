#include "Server.hpp"

#include <string.h>
#include <time.h>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Request.hpp"
#include "Utils.hpp"

#define READ_SZ 8192
#define SOCKET_CLOSED -32

const char *http_400_content =
    "HTTP/1.0 400 Bad Request\r\n"
    "Content-type: text/plain\r\n"
    "\r\n"
    "Bad Request \r\n";

const char *http_405_content =
    "HTTP/1.0 405 Method Not Allowed\r\n"
    "Content-type: text/plain\r\n"
    "\r\n"
    "Method Not Allowed \r\n";

const char *http_502_content =
    "HTTP/1.0 502 Bad Gateway\r\n"
    "Content-type: text/plain\r\n"
    "\r\n"
    "Bad Gateway \r\n";

const char *http_504_content =
    "HTTP/1.0 504 Gateway Timeout\r\n"
    "Content-type: text/plain\r\n"
    "\r\n"
    "Gateway Timeout \r\n";

Server::Server() { ring_ = nullptr; }

void Server::SetRing(struct io_uring *ring) { ring_ = ring; }

void Server::AddReadRequest(int client_socket) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   struct Request *req = Utils::HttpEntryRequest();
   req->iov[0].iov_base = malloc(READ_SZ);
   req->iov[0].iov_len = READ_SZ;
   req->event_type = EVENT_TYPE_SERVER_READ;
   req->client_socket = client_socket;
   memset(req->iov[0].iov_base, 0, READ_SZ);
   /* Linux kernel 5.5 has support for readv, but not for recv() or read() */
   io_uring_prep_readv(sqe, client_socket, &req->iov[0], 1, 0);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);
}

bool Server::HandleRead(struct Request *request,
                        struct Request *inner_request) {
   char http_command[1024];
   char header[2048];
   if (FetchHeader((char *)request->iov[0].iov_base, http_command, header,
                   sizeof(header))) {
      Log(__FILE__, __LINE__, Log::kError) << "Malformed request";
      AddHttpErrorRequest(request->client_socket, 400);
      Utils::ReleaseRequest(request);
      return false;
   }
   char *method, *path, *saveptr;

   method = strtok_r(http_command, " ", &saveptr);
   Utils::StrToLower(method);
   path = strtok_r(NULL, " ", &saveptr);

   inner_request->iov[0].iov_base = malloc(strlen(path) + 1);
   inner_request->iov[0].iov_len = strlen(path) + 1;
   strcpy((char *)inner_request->iov[0].iov_base, path);

   inner_request->iov[5].iov_base = malloc(strlen(header) + 1);
   inner_request->iov[5].iov_len = strlen(header) + 1;
   strcpy((char *)inner_request->iov[5].iov_base, header);

   inner_request->client_socket = request->client_socket;

   if (strcmp(method, "get") == 0) {
      Utils::ReleaseRequest(request);
      return true;
   }
   AddHttpErrorRequest(request->client_socket, 405);
   Utils::ReleaseRequest(request);

   return false;
}

void Server::AddHttpErrorRequest(int client_socket, int status_code) {
   struct Request *req = Utils::HttpErrorRequest();
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   req->event_type = EVENT_TYPE_SERVER_WRITE;
   req->client_socket = client_socket;

   const char *data;

   switch (status_code) {
      case 400:
         data = http_400_content;
         break;
      case 405:
         data = http_405_content;
         break;
      case 502:
         data = http_502_content;
         break;
      case 504:
         data = http_504_content;
         break;
   }

   unsigned long slen = strlen(data);
   req->iov[0].iov_base = Utils::ZhMalloc(slen);
   req->iov[0].iov_len = slen;
   memcpy(req->iov[0].iov_base, data, slen);
   io_uring_prep_write(sqe, req->client_socket, req->iov[0].iov_base,
                       req->iov[0].iov_len, 0);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);
}

int Server::FetchHeader(const char *src, char *command, char *header,
                        int dest_sz) {
   int offset = 0;
   while (offset < dest_sz) {
      if (src[offset] == '\r' && src[offset + 1] == '\n' &&
          src[offset + 2] == '\r' && src[offset + 3] == '\n') {
         offset += 4;
         break;
      }
      offset++;
   }

   if (offset >= dest_sz) {
      return 1;
   }

   std::string header_raw(src);
   std::size_t pos = header_raw.find("\r\n");
   std::string first_line = header_raw.substr(0, pos);
   std::string content = header_raw.substr(pos + 2);

   std::string pivot = "Host: ";
   pos = content.find(pivot);
   std::string buffer = content.substr(0, pos);
   content = content.substr(pos + pivot.length());
   pos = content.find("\r\n");
   buffer = buffer + content.substr(pos + 2);

   strcpy(command, first_line.c_str());
   strcpy(header, buffer.c_str());

   return 0;
}

void Server::AddWriteRequest(struct Request *req, bool is_stream) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   if (is_stream) {
      req->event_type = EVENT_TYPE_SERVER_WRITE_STREAM;
   } else {
      req->event_type = EVENT_TYPE_SERVER_WRITE;
   }
   io_uring_prep_write(sqe, req->client_socket, req->iov[3].iov_base,
                       req->iov[3].iov_len, 0);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);
}

void Server::HandleWrite(struct Request *request, int response) {
   if (response != SOCKET_CLOSED) {
      close(request->client_socket);
   }
   Utils::ReleaseRequest(request);
}

int Server::HandleWriteStream(struct Request *request, int response) {
   return response == SOCKET_CLOSED ? 1 : 0;
}

void Server::AddCloseRequest(struct Request *request) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   request->event_type = EVENT_TYPE_SERVER_CLOSE;
   io_uring_prep_nop(sqe);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);
}

void Server::HandleClose(struct Request *request) {
   Log(__FILE__, __LINE__) << "Socket close";
   close(request->client_socket);
   Utils::ReleaseRequest(request);
}
