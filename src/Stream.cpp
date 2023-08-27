#include "Stream.hpp"

#include <fcntl.h>
#include <liburing.h>

#include <cstring>
#include <iostream>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Request.hpp"
#include "Settings.hpp"
#include "Utils.hpp"
#include "xxhash64.h"

#define BUFFER_SIZE 327680

Stream::Stream() { server_ = nullptr; }

void Stream::SetServer(Server *server) { server_ = server; }

void Stream::SetRing(struct io_uring *ring) { ring_ = ring; }

bool Stream::HandleExistsResource(struct Request *entry) {
   if (resources_.find(entry->resource_id) != resources_.end()) {
      std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< "aa" << std::endl;
      struct Mux *mux = resources_.at(entry->resource_id);
      std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< "aa" << std::endl;

      std::string header_data((char *)entry->iov[2].iov_base);

      std::string etag = Utils::GetHeaderTag(header_data, "If-Match");
      if (!etag.empty()) {
         if (etag != mux->etag) {
            server_->AddHttpErrorRequest(entry->client_socket, 412);
            return true;
         }
      }

      etag = Utils::GetHeaderTag(header_data, "If-None-Match");
      if (!etag.empty()) {
         if (etag == mux->etag) {
            server_->AddHttpErrorRequest(entry->client_socket, 304);
            return true;
         }
      }

      struct Request *stream = Utils::StreamRequest(entry);
      std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< mux->requests.size() << std::endl;
      mux->requests.push_back(stream);
      if (mux->type != RESOURCE_TYPE_UNDEFINED) {
         AddWriteHeaders(stream, mux);
      }
      return true;
   }

   struct Request *stream = Utils::StreamRequest(entry);
   std::string url((char *)entry->iov[1].iov_base);
   struct Mux *mux = CreateMux(url);
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
   if (mux->requests.size() > 1) {
      stream->pivot = mux->requests.at(1)->pivot;
   } else {
      stream->pivot = mux->pivot;
   }
   memset(stream->iov[0].iov_base, 0, Settings::HttpBufferSize);

   if (mux->type == RESOURCE_TYPE_CACHE) {
      if (!mux->in_memory) {
         AddWriteFromCacheRequest(stream, mux);
      } else {
         AddWriteStreamRequest(stream);
      }
   } else if (mux->type == RESOURCE_TYPE_STREAMING) {
      AddWriteStreamRequest(stream);
   }
}

void Stream::SetCacheResource(uint64_t resource_id,
                              const std::string &header_data, std::string path,
                              bool is_completed) {
   struct Mux *mux = resources_.at(resource_id);
   mux->type = RESOURCE_TYPE_CACHE;

   mux->path = path;
   mux->is_completed = is_completed;

   mux->etag = Utils::GetHeaderTag(header_data, "ETag");

   mux->header.iov_len = header_data.size();
   mux->header.iov_base = malloc(mux->header.iov_len);
   memcpy(mux->header.iov_base, header_data.c_str(), mux->header.iov_len);

   for (struct Request *const c : mux->requests) {
      if (!c->is_processing) {
         AddWriteHeaders(c, mux);
      }
   }
}

void Stream::SetStreamingResource(uint64_t resource_id,
                                  const std::string &header_data) {
   struct Mux *mux = resources_.at(resource_id);
   mux->type = RESOURCE_TYPE_STREAMING;

   mux->header.iov_len = header_data.size();
   mux->header.iov_base = malloc(mux->header.iov_len);
   memcpy(mux->header.iov_base, header_data.c_str(), mux->header.iov_len);

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
   if (resources_.find(stream->resource_id) == resources_.end()) {
      return 1;
   }

   struct Mux *mux = resources_.at(stream->resource_id);

   // TODO : fix this
   /*
   if (stream->auxiliar > 0) {
      size_t size = mux->buffer[mux->pivot - 1].iov_len;
      stream->auxiliar -= size;
      return 1;
   }
   */

   size_t size = mux->buffer[stream->pivot].iov_len;
   if (size == EMPTY_BUFFER || size == 0) {
      if (mux->in_memory && mux->is_completed) {
         server_->AddCloseRequest(stream);
         RemoveRequest(stream);
         return 1;
      }
      //stream->auxiliar = BUFFER_SIZE;
      return 1;
   }

   unsigned int buffer_pivot = stream->pivot;

   stream->iov[0].iov_len = size;
   memcpy(stream->iov[0].iov_base, mux->buffer[buffer_pivot].iov_base,
          stream->iov[0].iov_len);

   buffer_pivot++;
   stream->pivot = buffer_pivot;
   stream->pivot %= Settings::StreamingBufferSize;

   server_->AddWriteRequest(stream, EVENT_TYPE_SERVER_WRITE_PARTIAL);

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

struct Mux *Stream::CreateMux(std::string url) {
   struct Mux *mux = new Mux();
   std::vector<struct Request *> requests;

   mux->requests = requests;
   mux->pivot = 0;
   mux->buffer = new iovec[Settings::StreamingBufferSize];
   mux->type = RESOURCE_TYPE_UNDEFINED;
   mux->header.iov_len = 0;
   mux->is_completed = false;
   mux->url = url;
   mux->in_memory = false;

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

   if (mux->type == RESOURCE_TYPE_STREAMING) {
      if (requests.empty()) {
         ReleaseResource(resource_id);
         return 1;
      }
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

struct Mux *Stream::GetResource(uint64_t resource_id) {
   return resources_.at(resource_id);
}

bool Stream::ExistsResource(uint64_t resource_id) {
   return resources_.find(resource_id) != resources_.end();
}

int Stream::NotifyCacheCompleted(uint64_t resource_id, struct iovec *buffer, int size) {
   if (resources_.find(resource_id) == resources_.end()) {
      return 1;
   }

   struct Mux *mux = resources_.at(resource_id);
   mux->is_completed = true;
   if (mux->in_memory) {
      if (size > 0) {
         for (int i = 0; i < size; i++) {
            size_t length = buffer[i].iov_len;
            mux->buffer[i].iov_base = malloc(length);
            memcpy(mux->buffer[i].iov_base, buffer[i].iov_base, length);
            mux->buffer[i].iov_len = length;
         }
         mux->pivot = mux->pivot + size;
      }
   }

   for (struct Request *const c : mux->requests) {
      if (!c->is_processing) {
         if (mux->in_memory) {
            AddWriteStreamRequest(c);
         } else {
            HandleCopyCacheRequest(c, 0);
         }
      }
   }
   return 0;
}

int Stream::AddWriteFromCacheRequest(struct Request *stream, struct Mux *mux) {
   int fd = open(mux->path.c_str(), O_RDONLY | O_NONBLOCK);

   if (fd < 0) {
      Log(__FILE__, __LINE__, Log::kError)
          << "wrong file " << mux->path << " " << strerror(errno);
      return 1;
   }

   stream->event_type = EVENT_TYPE_CACHE_READ_CONTENT;

   if (stream->iov[0].iov_len == 0) {
      stream->iov[0].iov_base = malloc(Settings::HttpBufferSize);
      stream->iov[0].iov_len = Settings::HttpBufferSize;
   }
   stream->auxiliar = fd;
   stream->is_processing = true;

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_read(sqe, stream->auxiliar, stream->iov[0].iov_base,
                      Settings::HttpBufferSize, 0);
   io_uring_sqe_set_data(sqe, stream);
   io_uring_submit(ring_);

   return 0;
}

bool Stream::HandleReadCacheRequest(struct Request *stream, int readed) {
   if (readed == 0) {
      struct Mux *mux = resources_.at(stream->resource_id);
      if (mux->is_completed) {
         close(stream->auxiliar);
         RemoveRequest(stream);
         server_->AddCloseRequest(stream);
      }
      return true;
   }
   stream->iov[0].iov_len = readed;
   stream->pivot += readed;
   server_->AddWriteRequest(stream, EVENT_TYPE_CACHE_COPY_CONTENT);

   return true;
}

int Stream::HandleCopyCacheRequest(struct Request *stream, int readed) {
   stream->is_processing = true;
   stream->event_type = EVENT_TYPE_CACHE_READ_CONTENT;
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   // Read from cache, auxiliar is the file descriptor
   io_uring_prep_read(sqe, stream->auxiliar, stream->iov[0].iov_base,
                      Settings::HttpBufferSize, stream->pivot);
   io_uring_sqe_set_data(sqe, stream);
   io_uring_submit(ring_);
   return 0;
}

void Stream::ProcessNext(struct Request *stream) {
   auto mux = GetResource(stream->resource_id);
   if (mux->in_memory) {
      AddWriteStreamRequest(stream);
   }
}
