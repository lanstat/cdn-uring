#include "Cache.hpp"

#include <cstring>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"
#include "xxhash64.h"

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

   int fd = open(path.c_str(), O_RDONLY);

   if (fd < 0) {
      Log(__FILE__, __LINE__, Log::kError)
          << "wrong file " << path << " " << strerror(errno);
      stream_->AbortStream(inner->resource_id);
      return;
   }

   //struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   //cache->iov[2].iov_base = malloc(Settings::HttpBufferSize);
   //cache->iov[2].iov_len = Settings::HttpBufferSize;
   //cache->event_type = EVENT_TYPE_CACHE_READ_HEADER;
   //memset(cache->iov[2].iov_base, 0, Settings::HttpBufferSize);

   //io_uring_prep_readv(sqe, fd, &cache->iov[2], 1, 0);
   //// io_uring_prep_read(sqe, fd, &request->iov[2].iov_base, stx->stx_size, 0);
   //io_uring_sqe_set_data(sqe, cache);
   //io_uring_submit(ring_);
}

int Cache::HandleReadHeader(struct Request *cache, int readed) {
   std::string path((char *)cache->iov[1].iov_base);
   cache->iov[2].iov_len = readed;
   stream_->SetCacheResource(cache->resource_id, cache, path);
   return 0;
}

void Cache::AddReadRequest(struct Request *request) {
   struct statx *stx = (struct statx *)request->iov[2].iov_base;

   char *path = (char *)request->iov[1].iov_base;

   int fd = open(path, O_RDONLY);

   if (fd < 0) {
      Log(__FILE__, __LINE__, Log::kError)
          << "wrong file " << path << " " << strerror(errno);
      stream_->ReleaseErrorAllWaitingRequest(request->resource_id, 502);
      return;
   }
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   request->iov[3].iov_base = malloc(stx->stx_size);
   request->iov[3].iov_len = stx->stx_size;
   request->event_type = EVENT_TYPE_CACHE_READ_CONTENT;
   memset(request->iov[3].iov_base, 0, stx->stx_size);

   /* Linux kernel 5.5 has support for readv, but not for recv() or read() */
   io_uring_prep_readv(sqe, fd, &request->iov[3], 1, 0);
   // io_uring_prep_read(sqe, fd, &request->iov[2].iov_base, stx->stx_size, 0);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);
}

int Cache::HandleRead(struct Request *request) {
   /*
   StoreFileInMemory(request);

   ReleaseAllWaitingRequest(request);
   */

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

   node->buffer[node->pivot].iov_len = EMPTY_BUFFER;

   if (!node->cache->is_processing) {
      HandleWrite(node->cache);
   }
   return true;
}

void Cache::AddWriteRequest(struct Request *cache) {}

int Cache::HandleWrite(struct Request *cache) {
   struct Node *node = nodes_.at(cache->resource_id);

   if (node->buffer[cache->pivot].iov_len == EMPTY_BUFFER) {
      return 1;
   }

   cache->iov[0].iov_len = node->buffer[cache->pivot].iov_len;
   memcpy(cache->iov[0].iov_base, node->buffer[cache->pivot].iov_base,
          cache->iov[0].iov_len);
   
   cache->pivot++;
   cache->pivot %= Settings::CacheBufferSize;

   cache->is_processing = true;
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   cache->event_type = EVENT_TYPE_CACHE_WRITE_CONTENT;

   io_uring_prep_write(sqe, cache->client_socket, cache->iov[0].iov_base,
                       cache->iov[0].iov_len, 0);
   io_uring_sqe_set_data(sqe, cache);
   io_uring_submit(ring_);

   //stream_->NotifyStream(uint64_t resource_id, void *buffer, int size)

   return 0;
}

bool Cache::GenerateNode(struct Request *http) {
   std::string path = GetCachePath(http->resource_id);
   int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
   if (fd < 0) {
      Log(__FILE__, __LINE__, Log::kError)
          << "wrong write file " << path << " " << strerror(errno);
      return false;
   }

   auto cache = Utils::CacheRequest(http);
   cache->client_socket = fd;

   cache->iov[0].iov_len = http->iov[0].iov_len;
   memcpy(cache->iov[0].iov_base, http->iov[0].iov_base, http->iov[0].iov_len);
   stream_->SetCacheResource(http->resource_id, cache, path);
   HandleWrite(cache);

   struct Node *node = (Node *)malloc(
       sizeof(*node) + sizeof(struct iovec) * Settings::CacheBufferSize);
   node->pivot = 0;
   std::pair<uint64_t, struct Node *> item(http->resource_id, node);
   nodes_.insert(item);

   return true;
}

void Cache::CloseBuffer(uint64_t resource_id) {

}
