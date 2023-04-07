#include "Stream.hpp"

#include <cstring>

#include "EventType.hpp"
#include "Request.hpp"
#include "Settings.hpp"
#include "Utils.hpp"
#include "xxhash64.h"

Stream::Stream() { server_ = nullptr; }

void Stream::SetServer(Server *server) { server_ = server; }

void Stream::SetRing(struct io_uring *ring) { ring_ = ring; }

bool Stream::HandleExistsResource(struct Request *entry) {
   struct Request *stream = Utils::StreamRequest(entry);

   if (resources_.find(stream->resource_id) != resources_.end()) {
      struct Mux *mux = resources_.at(stream->resource_id);
      stream->debug = true;
      mux->requests.push_back(stream);
      if (mux->type != RESOURCE_TYPE_UNDEFINED) {
         AddWriteHeaders(stream, mux);
      }
      return true;
   }

   struct Mux *mux = CreateMux();
   mux->requests.push_back(stream);
   std::pair<uint64_t, struct Mux *> item(stream->resource_id, mux);
   resources_.insert(item);

   return false;
}

void Stream::AddWriteHeaders(struct Request *stream, struct Mux *mux) {
   stream->iov[0].iov_base = malloc(Settings::HttpBufferSize);
   stream->iov[0].iov_len = mux->header.iov_len;
   memcpy(stream->iov[0].iov_base, mux->header.iov_base,
          stream->iov[0].iov_len);

   server_->AddWriteHeaderRequest(stream);
}

void Stream::HandleWriteHeaders(struct Request *stream) {
   struct Mux *mux = resources_.at(stream->resource_id);
   stream->pivot = mux->pivot;
}

void Stream::SetCacheResource(uint64_t resource_id, struct Request *cache,
                              std::string path) {
   struct Mux *mux = resources_.at(resource_id);
   mux->type = RESOURCE_TYPE_CACHE;

   mux->path = path;

   mux->header.iov_len = cache->iov[2].iov_len;
   mux->header.iov_base = malloc(cache->iov[2].iov_len);
   memcpy(mux->header.iov_base, cache->iov[2].iov_base, cache->iov[2].iov_len);

   for (struct Request *const c : mux->requests) {
      if (!c->is_processing) {
         AddWriteStreamRequest(c);
      }
   }
}

void Stream::SetStreamingResource(uint64_t resource_id, struct Request *http) {
   struct Mux *mux = resources_.at(resource_id);
   mux->type = RESOURCE_TYPE_STREAMING;

   mux->header.iov_len = http->iov[0].iov_len;
   mux->header.iov_base = malloc(http->iov[0].iov_len);
   memcpy(mux->header.iov_base, http->iov[0].iov_base, http->iov[0].iov_len);

   for (struct Request *const c : mux->requests) {
      if (!c->is_processing) {
         AddWriteHeaders(c, mux);
      }
   }
}

void Stream::AbortStream(uint64_t resource_id) {}

int Stream::NotifyStream(uint64_t resource_id, void *buffer, int size) {
   if (resources_.find(resource_id) == resources_.end()) {
      return 1;
   }

   struct Mux *mux = resources_.at(resource_id);

   const std::vector<struct Request *> &requests = mux->requests;

   if (requests.empty()) {
      ReleaseResource(resource_id);
      return 1;
   }

   unsigned int pivot = mux->pivot;
   if (mux->buffer[pivot].iov_len == 0) {
      mux->buffer[pivot].iov_base = malloc(Settings::HttpBufferSize);
   }

   mux->buffer[pivot].iov_len = size;
   memcpy(mux->buffer[pivot].iov_base, buffer, size);

   mux->pivot++;
   mux->pivot %= Settings::StreamingBufferSize;

   if (mux->buffer[mux->pivot].iov_len != 0) {
      mux->buffer[mux->pivot].iov_len = EMPTY_BUFFER;
   }

   for (struct Request *const c : mux->requests) {
      if (!c->is_processing) {
         AddWriteStreamRequest(c);
      }
   }
   return 0;
}

int Stream::AddWriteStreamRequest(struct Request *stream) {
   struct Mux *mux = resources_.at(stream->resource_id);

   size_t size = mux->buffer[stream->pivot].iov_len;

   if (size == EMPTY_BUFFER || size == 0) {
      return 1;
   }

   unsigned int buffer_pivot = stream->pivot;

   stream->iov[0].iov_len = size;
   memcpy(stream->iov[0].iov_base, mux->buffer[buffer_pivot].iov_base,
          stream->iov[0].iov_len);

   buffer_pivot++;
   stream->pivot = buffer_pivot;
   stream->pivot %= Settings::StreamingBufferSize;

   server_->AddWriteRequest(stream, true);

   return 0;
}

void Stream::ReleaseErrorAllWaitingRequest(uint64_t resource_id,
                                           int status_code) {
   struct Mux *mux = resources_.at(resource_id);

   const std::vector<struct Request *> &requests = mux->requests;

   for (struct Request *const c : requests) {
      server_->AddHttpErrorRequest(c->client_socket, status_code);
      Utils::ReleaseRequest(c);
   }

   ReleaseResource(resource_id);
}

struct Mux *Stream::CreateMux() {
   struct Mux *mux = new Mux();
   std::vector<struct Request *> requests;

   mux->requests = requests;
   mux->pivot = 0;
   mux->buffer = new iovec[Settings::StreamingBufferSize];
   mux->type = RESOURCE_TYPE_UNDEFINED;
   mux->header.iov_len = 0;

   for (int i = 0; i < Settings::StreamingBufferSize; i++) {
      mux->buffer[i].iov_len = 0;
   }
   
   return mux;
}

void Stream::ReleaseResource(uint64_t resource_id) {
   struct Mux *mux = resources_.at(resource_id);

   if (mux->header.iov_len > 0) {
      free(mux->header.iov_base);
   }

   int size = Settings::StreamingBufferSize;
   for (int i = 0; i < size; i++) {
      if (mux->buffer[i].iov_len > 0) {
         free(mux->buffer[i].iov_base);
      }
   }

   delete[] mux->buffer;
   delete mux;
   resources_.erase(resource_id);
}

int Stream::RemoveRequest(struct Request *request) {
   auto resource_id = request->resource_id;

   struct Mux *mux = resources_.at(resource_id);
   std::vector<struct Request *> requests = mux->requests;
   int pointer = 0;
   for (struct Request *const c : requests) {
      if (c == request) {
         break;
      }
      pointer++;
   }

   requests.erase(requests.begin() + pointer);
   mux->requests = requests;

   if (requests.empty()) {
      ReleaseResource(resource_id);
      return 1;
   }
   return 0;
}

void Stream::CloseStream(uint64_t resource_id) {
   struct Mux *mux = resources_.at(resource_id);
   const std::vector<struct Request *> &requests = mux->requests;
   for (struct Request *const c : requests) {
      server_->AddCloseRequest(c);
   }

   ReleaseResource(resource_id);
}
