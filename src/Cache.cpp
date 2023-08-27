#include "Cache.hpp"

#include <fcntl.h>
#include <liburing.h>
#include <sys/stat.h>

#include <cstring>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"
#include "xxhash64.h"
#include "Helpers.hpp"

#define INVALID_FILE 13816973012072644543ULL

Cache::Cache() { stream_ = nullptr; }

void Cache::SetRing(struct io_uring *ring) { ring_ = ring; }

void Cache::SetStream(Stream *stream) { stream_ = stream; }

std::string Cache::GetCachePath(uint64_t resource_id) {
   std::string uri;
   uri.append(Settings::CacheDir);
   uri.append(std::to_string(resource_id));
   return uri;
}

void Cache::AddExistsRequest(struct Request *entry) {
   std::string path = GetCachePath(entry->resource_id);

   struct Request *inner = Utils::InnerRequest(entry);

   struct statx stx;
   size_t size = sizeof(stx);
   inner->iov[0].iov_len = size;
   inner->iov[0].iov_base = malloc(size);
   memset(inner->iov[0].iov_base, 0xbf, size);

   inner->iov[1].iov_base = malloc(path.length() + 1);
   inner->iov[1].iov_len = path.length() + 1;
   strcpy((char *)inner->iov[1].iov_base, path.c_str());

   // Client url
   inner->iov[2].iov_base = malloc(entry->iov[1].iov_len);
   inner->iov[2].iov_len = entry->iov[1].iov_len;
   memcpy(inner->iov[2].iov_base, entry->iov[1].iov_base,
          inner->iov[2].iov_len);

   // Copy client header
   inner->iov[3].iov_base = malloc(entry->iov[2].iov_len);
   inner->iov[3].iov_len = entry->iov[2].iov_len;
   memcpy(inner->iov[3].iov_base, entry->iov[2].iov_base,
          inner->iov[3].iov_len);

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   inner->event_type = EVENT_TYPE_CACHE_EXISTS;
   io_uring_prep_statx(sqe, 0, (char *)inner->iov[1].iov_base,
                       AT_STATX_SYNC_AS_STAT, STATX_SIZE | STATX_INO,
                       (struct statx *)inner->iov[0].iov_base);
   io_uring_sqe_set_data(sqe, inner);
   io_uring_submit(ring_);
}

bool Cache::HandleExists(struct Request *inner) {
   struct statx *stx = (struct statx *)inner->iov[0].iov_base;

   if (stx->stx_ino == INVALID_FILE) return false;

   return true;
}

void Cache::AddReadHeaderRequest(struct Request *inner) {
   std::string path = GetCachePath(inner->resource_id);
   path = path + "_h";

   int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);

   if (fd < 0) {
      Log(__FILE__, __LINE__, Log::kError)
          << "wrong file " << path << " " << strerror(errno);
      stream_->AbortStream(inner->resource_id);
      return;
   }

   struct Request *cache = Utils::CacheRequest(inner);
   cache->event_type = EVENT_TYPE_CACHE_READ_HEADER;
   cache->client_socket = fd;

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_read(sqe, fd, cache->iov[0].iov_base, cache->iov[0].iov_len,
                      0);
   io_uring_sqe_set_data(sqe, cache);
   io_uring_submit(ring_);
}

int Cache::HandleReadHeader(struct Request *cache, int readed) {
   std::string path = GetCachePath(cache->resource_id);
   std::string header_data((char *)cache->iov[0].iov_base);
   stream_->SetCacheResource(cache->resource_id, header_data, path, true);

   Helpers::CloseFD(cache->client_socket);
   Utils::ReleaseRequest(cache);
   return 0;
}

bool Cache::AppendBuffer(uint64_t resource_id, void *buffer, int length) {
   struct Node *node = nodes_.at(resource_id);
   if (node->buffer[node->pivot].iov_len == 0) {
      node->buffer[node->pivot].iov_base = malloc(Settings::HttpBufferSize);
   }

   node->buffer[node->pivot].iov_len = length;
   memcpy(node->buffer[node->pivot].iov_base, buffer, length);

   node->pivot++;
   node->pivot %= Settings::CacheBufferSize;

   if (node->buffer[node->pivot].iov_len != 0) {
      node->buffer[node->pivot].iov_len = EMPTY_BUFFER;
   }

   if (!node->cache->is_processing) {
      HandleWrite(node->cache);
   }
   return true;
}

bool Cache::AddWriteHeaderRequest(struct Request *header, std::string path) {
   path = path + "_h";
   int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
   if (fd < 0) {
      Log(__FILE__, __LINE__, Log::kError)
          << "wrong write file " << path << " " << strerror(errno);
      return false;
   }

   header->client_socket = fd;

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   header->event_type = EVENT_TYPE_CACHE_WRITE_HEADER;

   io_uring_prep_write(sqe, header->client_socket, header->iov[0].iov_base,
                       header->iov[0].iov_len, 0);
   io_uring_sqe_set_data(sqe, header);
   io_uring_submit(ring_);

   return true;
}

bool Cache::HandleWriteHeader(struct Request *cache, int writed) {
   Helpers::CloseFD(cache->client_socket);
   Utils::ReleaseRequest(cache);
   return true;
}

void Cache::AddWriteRequest(struct Request *cache) {}

int Cache::HandleWrite(struct Request *cache) {
   if (nodes_.find(cache->resource_id) == nodes_.end()) {
      return 1;
   }

   struct Node *node = nodes_.at(cache->resource_id);

   size_t size = node->buffer[cache->pivot].iov_len;

   if (size == EMPTY_BUFFER || size == 0) {
      return 1;
   }

   if (size == END_BUFFER) {
      ReleaseBuffer(cache->resource_id);
      return 0;
   }

   cache->iov[0].iov_len = size;
   memcpy(cache->iov[0].iov_base, node->buffer[cache->pivot].iov_base,
          cache->iov[0].iov_len);

   cache->pivot++;
   cache->pivot %= Settings::CacheBufferSize;

   node->size += size;

   auto mux = stream_->GetResource(cache->resource_id);
   if (mux->in_memory) {
      return 0;
   }

   cache->is_processing = true;
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   cache->event_type = EVENT_TYPE_CACHE_WRITE_CONTENT;

   io_uring_prep_write(sqe, cache->client_socket, cache->iov[0].iov_base,
                       cache->iov[0].iov_len, node->size);
   io_uring_sqe_set_data(sqe, cache);
   io_uring_submit(ring_);

   return 0;
}

bool Cache::GenerateNode(struct Request *http, std::string header_buffer) {
   std::string path;
   auto mux = stream_->GetResource(http->resource_id);
   auto cache = Utils::CacheRequest(http);

   if (!mux->in_memory) {
      path = GetCachePath(http->resource_id);
      int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (fd < 0) {
         Utils::ReleaseRequest(cache);
         Log(__FILE__, __LINE__, Log::kError)
             << "wrong write file " << path << " " << strerror(errno);
         return false;
      }
      cache->client_socket = fd;
   }

   auto header = Utils::CacheRequest(http);
   header->iov[0].iov_len = header_buffer.size();

   memcpy(header->iov[0].iov_base, header_buffer.c_str(),
          header->iov[0].iov_len);

   stream_->SetCacheResource(http->resource_id, header_buffer, path, false);

   if (!mux->in_memory) {
      AddWriteHeaderRequest(header, path);
   }

   struct Node *node = CreateNode();
   node->cache = cache;
   std::pair<uint64_t, struct Node *> item(http->resource_id, node);
   nodes_.insert(item);

   return true;
}

void Cache::CloseBuffer(uint64_t resource_id) {
   struct Node *node = nodes_.at(resource_id);
   if (node->buffer[node->pivot].iov_len == 0) {
      node->buffer[node->pivot].iov_base = malloc(1);
   }

   node->buffer[node->pivot].iov_len = END_BUFFER;

   auto mux = stream_->GetResource(resource_id);
   if (mux->in_memory) {
      node->cache->event_type = EVENT_TYPE_CACHE_WRITE_CONTENT;
      Helpers::SendRequestNop(ring_, node->cache, 100);
   }
}

void Cache::ReleaseBuffer(uint64_t resource_id) {
   struct Node *node = nodes_.at(resource_id);

   int size = Settings::CacheBufferSize;
   int length = 0;
   for (int i = 0; i < size; i++) {
      if (node->buffer[i].iov_len == EMPTY_BUFFER ||
          node->buffer[i].iov_len == END_BUFFER) {
         length = i;
         break;
      }
   }

   stream_->NotifyCacheCompleted(resource_id, node->buffer, length);

   for (int i = 0; i < size; i++) {
      if (node->buffer[i].iov_len > 0) {
         free(node->buffer[i].iov_base);
      }
   }

   Helpers::CloseFD(node->cache->client_socket);

   delete[] node->buffer;
   delete node;
   nodes_.erase(resource_id);
}

struct Node *Cache::CreateNode() {
   struct Node *node = new Node();
   node->pivot = 0;
   node->size = 0;
   node->buffer = new iovec[Settings::CacheBufferSize];

   for (int i = 0; i < Settings::CacheBufferSize; i++) {
      node->buffer[i].iov_len = 0;
   }

   return node;
}
