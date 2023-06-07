#include "Buffer.hpp"

#include "Settings.hpp"
#include "Logger.hpp"

Buffer::Buffer() {
   struct Request *request = CreateRequest(Settings::ReservedBufferSize);
   requests_.push_back(request);
}

Buffer::~Buffer() {}

void Buffer::SetRing(struct io_uring *ring) { ring_ = ring; }

struct Request *Buffer::CreateRequest(int iovec_count) {
   struct Request *req = (Request *)malloc(sizeof(*req));
   req->iovec_count = iovec_count;
   req->pivot = 0;
   req->index = 0;
   req->status = REQUEST_STATUS_UNASSIGNED;
   req->debug = false;
   return req;
}

struct Request *Buffer::AssignRequest(int iovec_count) {
   for (int i = 0; i < requests_.size(); i++) {
      if (requests_.at(i)->status != REQUEST_STATUS_UNASSIGNED) {
         continue;
      }

      if (requests_.at(i)->iovec_count == iovec_count) {
         return requests_.at(i);
      } else if (requests_.at(i)->iovec_count > iovec_count) {
         auto previous = requests_.at(i);
         previous->index += iovec_count;

         auto current = CreateRequest(iovec_count);
         requests_.insert(requests_.begin() + i, current);
         return current;
      }
   }
}

void Buffer::DeFragmentationTable() {}

void Buffer::ReserveMemory() {
   int buffer_count = Settings::ReservedBufferSize;
   int buffer_size = Settings::HttpBufferSize;
   iov_ = new iovec[buffer_count]();

   for (int i = 0; i < buffer_count; i++) {
      iov_[i].iov_base = malloc(buffer_size);
      iov_[i].iov_len = buffer_size;
      memset(iov_[i].iov_base, 0, buffer_size);
   }

   int result = io_uring_register_buffers(ring_, iov_, buffer_count);

   if (result) {
      Log(__FILE__, __LINE__, Log::kError)
          << "Error registering buffers" << strerror(-result);
      exit(1);
   }
}

void Buffer::ReleaseRequest(struct Request *request) {
   request->status = REQUEST_STATUS_UNASSIGNED;
}

/*
 * Object for entry http request
 *
 * 0 = char* request's header
 * 1 = resource http path
 * 2 = header content
 */
struct Request *Buffer::HttpEntryRequest() { return AssignRequest(4); }

/*
 * Object for http error request
 *
 * 0 = char* bytes with the error response
 */
struct Request *Buffer::HttpErrorRequest() { return AssignRequest(1); }

/*
 * Object for http external request
 *
 * 0 = void* bytes bytes readed for the request
 */
struct Request *Buffer::HttpExternalRequest(struct Request *cache) {
   struct Request *request = AssignRequest(1);
   request->resource_id = cache->resource_id;

   return request;
}

/*
 * Object for http secure external request
 *
 * 0 = void* bytes bytes readed for the request
 * 1 = SSL* ssl pointer
 * 2 = SSL_CTX* ssl context
 */
struct Request *Buffer::HttpsExternalRequest(struct Request *inner) {
   struct Request *request = AssignRequest(4);
   request->resource_id = inner->resource_id;

   return request;
}

/*
 * Object for stream request
 *
 * 0 = void* bytes to r/w to client socket
 */
struct Request *Buffer::StreamRequest(struct Request *entry) {
   struct Request *request = AssignRequest(1);
   request->resource_id = entry->resource_id;
   request->client_socket = entry->client_socket;

   return request;
}

/*
 * Object for cache request
 *
 * 0 = statx* status of the resource in cache
 * 1 = char* guid for the cache resource
 * 2 = char* path requested by the client eg. google.com/images
 * 3 = char* client header
 * 4 = sockaddr_in|sockaddr_in6 * pointer to external server
 */
struct Request *Buffer::InnerRequest(struct Request *entry) {
   struct Request *request = AssignRequest(8);
   request->resource_id = entry->resource_id;

   return request;
}

/*
 * Object for stream request
 *
 * 0 = void* bytes to w to client socket
 */
struct Request *Buffer::CacheRequest(struct Request *entry) {
   struct Request *request = AssignRequest(1);
   request->resource_id = entry->resource_id;

   return request;
}
