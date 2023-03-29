#include "Utils.hpp"

#include <climits>

/*
 * Utility function to convert a string to lower case.
 * */
void Utils::StrToLower(char *str) {
   for (; *str; ++str) *str = (char)tolower(*str);
}

/*
 * Helper function for cleaner looking code.
 * */
void *Utils::ZhMalloc(size_t size) {
   void *buf = malloc(size);
   if (!buf) {
      fprintf(stderr, "Fatal error: unable to allocate memory.\n");
      exit(1);
   }
   return buf;
}

struct Request *Utils::CreateRequest(int iovec_count) {
   struct Request *req =
       (Request *)malloc(sizeof(*req) + sizeof(struct iovec) * iovec_count);
   req->iovec_count = iovec_count;
   for (int i = 0; i < req->iovec_count; i++) {
      req->iov[i].iov_len = 0;
   }
   req->packet_count = 0;
   req->is_processing = false;
   req->debug = false;
   return req;
}

/*
 * Object for entry http request
 *
 * 0 = char* request's header
 */
struct Request *Utils::HttpEntryRequest() {
   return CreateRequest(1);
}

/*
 * Object for http error request 
 *
 * 0 = char* bytes with the error response
 */
struct Request *Utils::HttpErrorRequest() {
   return CreateRequest(1);
}

/*
 * Object for http inner request 
 *
 * 0 = char* path requested by the client eg. google.com/images
 * 1 = char* guid for the cache resource
 * 2 = statx* status of the resource in cache
 * 3 = void* response content
 * 4 = sockaddr_in|sockaddr_in6 * pointer to external server
 * 5 = char* header of the external client
 */
struct Request *Utils::HttpInnerRequest() {
   return CreateRequest(6);
}

/*
 * Object for http external request 
 *
 * 0 = void* bytes bytes readed for the request
 */
struct Request *Utils::HttpExternalRequest() {
   return CreateRequest(1);
}

/*
 * Object for http secure external request 
 *
 * 0 = void* bytes bytes readed for the request
 * 1 = SSL* ssl pointer
 * 2 = SSL_CTX* ssl context
 */
struct Request *Utils::HttpsExternalRequest() {
   return CreateRequest(3);
}

void Utils::ReleaseRequest(struct Request *request) {
   for (int i = 0; i < request->iovec_count; i++) {
      if (request->iov[i].iov_len > 0) {
         free(request->iov[i].iov_base);
      }
   }
   free(request);
}
