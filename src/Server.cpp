#include "Server.hpp"

#include <string.h>
#include <time.h>

#include "EventType.hpp"
#include "Request.hpp"
#include "Utils.hpp"

#define READ_SZ 8192

Server::Server() { ring_ = nullptr; }

void Server::SetRing(struct io_uring *ring) { ring_ = ring; }

void Server::AddReadRequest(int client_socket) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   struct Request *req = (Request *)malloc(sizeof(*req) + sizeof(struct iovec));
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

struct Request *Server::HandleRead(struct Request *request) {
   char http_request[1024];
   /* Get the first line, which will be the Request */
   if (GetLine((char *)request->iov[0].iov_base, http_request,
               sizeof(http_request))) {
      fprintf(stderr, "Malformed request\n");
      exit(1);
   }
   char *method, *path, *saveptr;

   method = strtok_r(http_request, " ", &saveptr);
   Utils::StrToLower(method);
   path = strtok_r(NULL, " ", &saveptr);

   auto response = Utils::CreateRequest(3);
   response->iovec_count = 3;
   response->iov[0].iov_base = path;
   response->iov[0].iov_len = strlen(path);

   response->iov[1].iov_base = method;
   response->iov[1].iov_len = strlen(method);

   response->client_socket = request->client_socket;

   free(request->iov[0].iov_base);
   return response;
}

void Server::HandleHttpMethod(char *method_buffer, int client_socket) {
   char *method, *path, *saveptr;

   method = strtok_r(method_buffer, " ", &saveptr);
   Utils::StrToLower(method);
   path = strtok_r(NULL, " ", &saveptr);

   // if (strcmp(method, "get") == 0) {
   // HandleGetMethod(path, client_socket);
   //} else {
   // HandleUnimplementedMethod(client_socket);
   //}
}

int Server::GetLine(const char *src, char *dest, int dest_sz) {
   for (int i = 0; i < dest_sz; i++) {
      dest[i] = src[i];
      if (src[i] == '\r' && src[i + 1] == '\n') {
         dest[i] = '\0';
         return 0;
      }
   }
   return 1;
}

void Server::HandleUnimplementedMethod(int client_socket) {
   // SendStaticStringContent(unimplemented_content, client_socket);
}

void Server::SendStaticStringContent(const char *str, int client_socket) {
   struct Request *req =
       (Request *)Utils::ZhMalloc(sizeof(*req) + sizeof(struct iovec));
   unsigned long slen = strlen(str);
   req->iovec_count = 1;
   req->client_socket = client_socket;
   req->iov[0].iov_base = Utils::ZhMalloc(slen);
   req->iov[0].iov_len = slen;
   memcpy(req->iov[0].iov_base, str, slen);
}

void Server::AddWriteRequest(struct Request *req) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   req->event_type = EVENT_TYPE_SERVER_WRITE;
   io_uring_prep_writev(sqe, req->client_socket, req->iov, req->iovec_count, 0);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);
}

int Server::HandleWrite(struct Request *request) {
   for (int i = 2; i < request->iovec_count; i++) {
      free(request->iov[i].iov_base);
   }
}
