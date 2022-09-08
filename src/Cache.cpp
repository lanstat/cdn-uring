#include "Cache.hpp"

#include "EventType.hpp"
#include "Md5.hpp"

#define INVALID_FILE 13816973012072644543

std::string PATH = "/home/lanstat/projects/confiared/my-cdn/cache/";

Cache::Cache() {}

void Cache::SetRing(struct io_uring *ring) { ring_ = ring; }

char *Cache::GetUID(char *url) {
   std::string aux(url);
   auto uid = md5(aux);
   std::string uri;
   uri.append(PATH);
   uri.append(uid);
   return strdup(uri.c_str());
}

void Cache::AddExistsRequest(struct Request *request) {
   struct statx stx;
   size_t size = sizeof(stx);
   request->iov[2].iov_base = malloc(size);
   request->iov[2].iov_len = size;
   memset(request->iov[2].iov_base, 0xbf, size);
   request->iovec_count = 3;

   char *path = GetUID((char *)request->iov[0].iov_base);
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   request->event_type = EVENT_TYPE_CACHE_EXISTS;
   io_uring_prep_statx(sqe, 0, path, AT_STATX_SYNC_AS_STAT,
                       STATX_SIZE | STATX_INO,
                       (struct statx *)request->iov[2].iov_base);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);

   //delete path;
}

int Cache::HandleExists(struct Request *request) {
   struct statx *stx = (struct statx *)request->iov[2].iov_base;

   if (stx->stx_ino == INVALID_FILE) return 1;
   return 0;
}

void Cache::AddReadRequest(struct Request *request) {
   struct statx *stx = (struct statx *)request->iov[2].iov_base;
   free(request->iov[2].iov_base);
   char *path = GetUID((char *)request->iov[0].iov_base);

   int fd = open(path, O_RDONLY);

   if (fd < 0) {
      std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< "asdasd" << std::endl;
      exit(1);
   }
   std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< stx->stx_size << std::endl;
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   request->iov[2].iov_base = malloc(stx->stx_size);
   request->iov[2].iov_len = stx->stx_size;
   request->event_type = EVENT_TYPE_CACHE_READ;
   memset(request->iov[2].iov_base, 0, stx->stx_size);

   /* Linux kernel 5.5 has support for readv, but not for recv() or read() */
   io_uring_prep_readv(sqe, fd, &request->iov[2], 1, 0);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);
}

int Cache::HandleRead(struct Request *request) {
   return 0;
}
