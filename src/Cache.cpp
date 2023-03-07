#include "Cache.hpp"

#include <cstring>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "Settings.hpp"
#include "xxhash64.h"

#define INVALID_FILE 13816973012072644543ULL

Cache::Cache() { server_ = nullptr; }

void Cache::SetRing(struct io_uring *ring) { ring_ = ring; }

void Cache::SetServer(Server *server) { server_ = server; }

std::string Cache::GetUID(char *url) {
   std::string aux(url);
   auto uid = XXHash64::hash(url, aux.length(), 0);
   std::string uri;
   uri.append(Settings::CacheDir);
   uri.append(std::to_string(uid));
   return uri;
}

void Cache::AddExistsRequest(struct Request *request) {
   std::string path = GetUID((char *)request->iov[0].iov_base);

   // In the case of the file is cached
   if (files_.find(path) != files_.cend() && Settings::UseCache) {
      Log(__FILE__, __LINE__) << "Reading from cache";
      struct File file = files_.at(path);
      AddCopyRequest(request, &file);
      return;
   } else {
      // If there is other request fetching the file wait until finished
      if (waiting_read_.find(path) != waiting_read_.cend()) {
         Log(__FILE__, __LINE__) << "Waiting cache";
         waiting_read_.at(path).push_back(request);
         return;
      }
      Log(__FILE__, __LINE__) << "Fetching cache";
      std::vector<struct Request *> requests;
      std::pair<std::string, std::vector<struct Request *>> item(path,
                                                                 requests);
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
   char *path = (char *)request->iov[1].iov_base;

   int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

   if (fd < 0) {
      Log(__FILE__, __LINE__, Log::kError)
          << "wrong write file " << path << " " << strerror(errno);
      ReleaseErrorAllWaitingRequest(request, 502);
      return;
   }

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   request->event_type = EVENT_TYPE_CACHE_WRITE;

   io_uring_prep_write(sqe, fd, request->iov[3].iov_base,
                       request->iov[3].iov_len, 0);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);
}

int Cache::HandleWrite(struct Request *request) {
   StoreFileInMemory(request);
   ReleaseAllWaitingRequest(request);
   return 0;
}

void Cache::AddCopyRequest(struct Request *request, File *file) {
   request->iov[3].iov_base = malloc(file->size);
   request->iov[3].iov_len = file->size;

   memcpy(request->iov[3].iov_base, file->data, file->size);

   server_->AddWriteRequest(request);
}

void Cache::StoreFileInMemory(struct Request *request) {
   std::string path = GetUID((char *)request->iov[0].iov_base);
   struct File file;
   file.data = malloc(request->iov[3].iov_len);
   file.size = request->iov[3].iov_len;
   memcpy(file.data, request->iov[3].iov_base, file.size);
   std::pair<std::string, struct File> item(path, file);
   files_.insert(item);
}

void Cache::ReleaseErrorAllWaitingRequest(struct Request *request, int status_code) {
   std::string path = GetUID((char *)request->iov[0].iov_base);
   server_->AddHttpErrorRequest(request->client_socket, status_code);
   Utils::ReleaseRequest(request);

   const std::vector<struct Request *> &requests = waiting_read_.at(path);
   for (struct Request *const c : requests) {
      server_->AddHttpErrorRequest(c->client_socket, status_code);
      Utils::ReleaseRequest(c);
   }
   waiting_read_.erase(path);
}

void Cache::ReleaseAllWaitingRequest(struct Request *request) {
   std::string path((char *)request->iov[1].iov_base);

   struct File file;
   file.data = malloc(request->iov[3].iov_len);
   file.size = request->iov[3].iov_len;
   memcpy(file.data, request->iov[3].iov_base, file.size);

   const std::vector<struct Request *> &requests = waiting_read_.at(path);
   for (struct Request *const c : requests) {
      AddCopyRequest(c, &file);
   }
   waiting_read_.erase(path);
}
