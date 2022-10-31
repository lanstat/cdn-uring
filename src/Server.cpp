#include "Server.hpp"

#include <string.h>
#include <time.h>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Request.hpp"
#include "Utils.hpp"

#define READ_SZ 8192

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
   struct Request *req = Utils::CreateRequest(1);
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
   char http_request[1024];
   /* Get the first line, which will be the Request */
   if (GetLine((char *)request->iov[0].iov_base, http_request,
               sizeof(http_request))) {
      Log(__FILE__, __LINE__, Log::kError) << "Malformed request";
      AddHttpErrorRequest(request->client_socket, 400);
      Utils::ReleaseRequest(request);
      return false;
   }
   char *method, *path, *saveptr;

   method = strtok_r(http_request, " ", &saveptr);
   Utils::StrToLower(method);
   path = strtok_r(NULL, " ", &saveptr);

   inner_request->iov[0].iov_base = malloc(strlen(path) + 1);
   inner_request->iov[0].iov_len = strlen(path) + 1;
   strcpy((char *)inner_request->iov[0].iov_base, path);
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
   struct Request *req = Utils::CreateRequest(3);
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   req->event_type = EVENT_TYPE_SERVER_WRITE;
   req->client_socket = client_socket;

   const char *data;

   std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< status_code << std::endl;

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
   req->iov[2].iov_base = Utils::ZhMalloc(slen);
   req->iov[2].iov_len = slen;
   memcpy(req->iov[2].iov_base, data, slen);
   io_uring_prep_write(sqe, req->client_socket, req->iov[2].iov_base,
                       req->iov[2].iov_len, 0);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);
}

int Server::GetLine(const char *src, char *dest, int dest_sz) {
   for (int i = 0; i < dest_sz; i++) {
      dest[i] = src[i];
      if (src[i] == '\r' && src[i + 1] == '\n') {
         return 0;
      }
   }
   Log(__FILE__, __LINE__) << src;
   return 1;
}

void Server::AddWriteRequest(struct Request *req) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   req->event_type = EVENT_TYPE_SERVER_WRITE;
   io_uring_prep_write(sqe, req->client_socket, req->iov[3].iov_base,
                       req->iov[3].iov_len, 0);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);
}

void Server::HandleWrite(struct Request *request) {
   close(request->client_socket);
   Utils::ReleaseRequest(request);
}
