#include "Cache.hpp"

#include <cstring>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"
#include "xxhash64.h"

#define INVALID_FILE 13816973012072644543ULL
#define BUFFER_PACKET_SIZE 10

Cache::Cache() { server_ = nullptr; }

void Cache::SetRing(struct io_uring *ring) { ring_ = ring; }

void Cache::SetServer(Server *server) { server_ = server; }

std::string Cache::GetUID(uint64_t resource_id) {
   std::string uri;
   uri.append(Settings::CacheDir);
   uri.append(std::to_string(resource_id));
   return uri;
}

uint64_t Cache::GetResourceId(char *url) {
   std::string aux(url);
   return XXHash64::hash(url, aux.length(), 0);
}

void Cache::AddExistsRequest(struct Request *request) {
   auto resource_id = GetResourceId((char *)request->iov[0].iov_base);
   std::string path = GetUID(resource_id);

   request->resource_id = resource_id;

   // In the case of the file is cached
   if (files_.find(path) != files_.cend() && Settings::UseCache) {
      Log(__FILE__, __LINE__) << "Reading from cache";
      struct File file = files_.at(path);
      AddCopyRequest(request, &file);
      return;
   } else {
      // If there is other request fetching the file wait until finished
      if (waiting_read_.find(resource_id) != waiting_read_.cend()) {
         Log(__FILE__, __LINE__) << "Waiting cache";

         request->debug = true;
         struct Mux *mux = waiting_read_.at(resource_id);
         mux->requests.push_back(request);
         return;
      }
      Log(__FILE__, __LINE__) << "Fetching cache";

      struct Mux *mux = CreateMux();

      mux->requests.push_back(request);

      std::pair<uint64_t, struct Mux *> item(resource_id, mux);
      waiting_read_.insert(item);
   }

   struct statx stx;
   size_t size = sizeof(stx);
   request->iov[2].iov_base = malloc(size);
   request->iov[2].iov_len = size;
   memset(request->iov[2].iov_base, 0xbf, size);

   request->iov[1].iov_base = malloc(path.length() + 1);
   request->iov[1].iov_len = path.length() + 1;
   strcpy((char *)request->iov[1].iov_base, path.c_str());

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   request->event_type = EVENT_TYPE_CACHE_EXISTS;
   io_uring_prep_statx(sqe, 0, (char *)request->iov[1].iov_base,
                       AT_STATX_SYNC_AS_STAT, STATX_SIZE | STATX_INO,
                       (struct statx *)request->iov[2].iov_base);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);
}

int Cache::HandleExists(struct Request *request) {
   struct statx *stx = (struct statx *)request->iov[2].iov_base;

   if (!Settings::UseCache) return 1;

   if (stx->stx_ino == INVALID_FILE) return 1;
   return 0;
}

void Cache::AddReadRequest(struct Request *request) {
   struct statx *stx = (struct statx *)request->iov[2].iov_base;

   char *path = (char *)request->iov[1].iov_base;

   int fd = open(path, O_RDONLY);

   if (fd < 0) {
      Log(__FILE__, __LINE__, Log::kError)
          << "wrong file " << path << " " << strerror(errno);
      ReleaseErrorAllWaitingRequest(request, 502);
      return;
   }
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   request->iov[3].iov_base = malloc(stx->stx_size);
   request->iov[3].iov_len = stx->stx_size;
   request->event_type = EVENT_TYPE_CACHE_READ;
   memset(request->iov[3].iov_base, 0, stx->stx_size);

   /* Linux kernel 5.5 has support for readv, but not for recv() or read() */
   io_uring_prep_readv(sqe, fd, &request->iov[3], 1, 0);
   // io_uring_prep_read(sqe, fd, &request->iov[2].iov_base, stx->stx_size, 0);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);
}

int Cache::HandleRead(struct Request *request) {
   StoreFileInMemory(request);

   ReleaseAllWaitingRequest(request);

   return 0;
}

void Cache::AddWriteRequest(struct Request *request) {
   /*
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   request->event_type = EVENT_TYPE_CACHE_WRITE;

   io_uring_prep_write(sqe, request->cache_socket, request->iov[3].iov_base,
                       request->iov[3].iov_len, 0);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);
   */
}

int Cache::HandleWrite(struct Request *request) {
   StoreFileInMemory(request);
   ReleaseAllWaitingRequest(request);
   return 0;
}

int Cache::AddWriteRequestStream(uint64_t resource_id, void *buffer, int size) {
   if (waiting_read_.find(resource_id) == waiting_read_.end()) {
      return 1;
   }

   struct Mux *mux = waiting_read_.at(resource_id);

   const std::vector<struct Request *> &requests = mux->requests;

   if (requests.empty()) {
      ReleaseResource(resource_id);
      return 1;
   }

   struct iovec packet;
   packet.iov_base = malloc(size);
   packet.iov_len = size;
   memcpy(packet.iov_base, buffer, size);

   // Store the packet in the buffer
   if (mux->count < BUFFER_PACKET_SIZE) {
      mux->header.push_back(packet);
   } else {
      std::pair<int, struct iovec> item(mux->count, packet);
      mux->buffer.insert(item);
   }

   mux->count++;

   for (struct Request *const c : requests) {
      if (!c->is_processing) {
         AddWriteRequestStream(c);
      }
   }

   return 0;
}

int Cache::AddWriteRequestStream(struct Request *request) {
   struct Mux *mux = waiting_read_.at(request->resource_id);

   if (mux->count <= request->packet_count) {
      return 1;
   }

   if (request->iov[3].iov_len == 0) {
      request->iov[3].iov_base = malloc(Settings::HttpBufferSize);
   }
   int buffer_pivot = request->packet_count;

   if (buffer_pivot < BUFFER_PACKET_SIZE) {
      request->iov[3].iov_len = mux->header.at(buffer_pivot).iov_len;
      memcpy(request->iov[3].iov_base, mux->header.at(buffer_pivot).iov_base,
             request->iov[3].iov_len);
   } else {
      if (mux->buffer.find(buffer_pivot) == mux->buffer.end()) {
         buffer_pivot = mux->count - 1;
      }
      request->iov[3].iov_len = mux->buffer.at(buffer_pivot).iov_len;
      memcpy(request->iov[3].iov_base, mux->buffer.at(buffer_pivot).iov_base,
             request->iov[3].iov_len);
   }
   buffer_pivot++;
   request->packet_count = buffer_pivot;

   server_->AddWriteRequest(request, true);

   return 0;
}

void Cache::CloseStream(uint64_t resource_id) {
   struct Mux *mux = waiting_read_.at(resource_id);
   const std::vector<struct Request *> &requests = mux->requests;
   for (struct Request *const c : requests) {
      server_->AddCloseRequest(c);
   }

   ReleaseResource(resource_id);
}

void Cache::AddCopyRequest(struct Request *request, File *file) {
   request->iov[3].iov_base = malloc(file->size);
   request->iov[3].iov_len = file->size;

   memcpy(request->iov[3].iov_base, file->data, file->size);

   server_->AddWriteRequest(request, false);
}

void Cache::StoreFileInMemory(struct Request *request) {
   std::string path((char *)request->iov[1].iov_base);
   struct File file;
   file.data = malloc(request->iov[3].iov_len);
   file.size = request->iov[3].iov_len;
   memcpy(file.data, request->iov[3].iov_base, file.size);
   std::pair<std::string, struct File> item(path, file);
   files_.insert(item);
}

void Cache::ReleaseErrorAllWaitingRequest(struct Request *request,
                                          int status_code) {
   int resource_id = request->resource_id;
   struct Mux *mux = waiting_read_.at(resource_id);

   const std::vector<struct Request *> &requests = mux->requests;

   for (struct Request *const c : requests) {
      server_->AddHttpErrorRequest(c->client_socket, status_code);
      Utils::ReleaseRequest(c);
   }

   ReleaseResource(resource_id);
}

void Cache::ReleaseAllWaitingRequest(struct Request *request) {
   auto resource_id = request->resource_id;

   struct File file;
   file.data = malloc(request->iov[3].iov_len);
   file.size = request->iov[3].iov_len;
   memcpy(file.data, request->iov[3].iov_base, file.size);

   struct Mux *mux = waiting_read_.at(resource_id);
   const std::vector<struct Request *> &requests = mux->requests;
   for (struct Request *const c : requests) {
      AddCopyRequest(c, &file);
   }

   ReleaseResource(resource_id);
}

int Cache::RemoveRequest(struct Request *request) {
   auto resource_id = request->resource_id;

   struct Mux *mux = waiting_read_.at(resource_id);
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

void Cache::ReleaseResource(uint64_t resource_id) {
   struct Mux *mux = waiting_read_.at(resource_id);
   int size = (int)mux->header.size();
   for (int i = 0; i < size; i++) {
      if (mux->header.at(i).iov_len > 0) {
         free(mux->header.at(i).iov_base);
      }
   }
   mux->header.clear();
   delete mux;
   waiting_read_.erase(resource_id);
}

struct Mux *Cache::CreateMux() {
   std::vector<struct Request *> requests;
   std::vector<struct iovec> header;
   std::unordered_map<int, struct iovec> buffer;

   struct Mux *mux = new Mux();
   mux->requests = requests;
   mux->header = header;
   mux->count = 0;
   mux->buffer = buffer;
   return mux;
}
