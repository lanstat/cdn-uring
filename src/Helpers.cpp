#include "Helpers.hpp"

#include <chrono>

#include "xxhash64.h"
#include "Settings.hpp"

uint64_t Helpers::GetResourceId(const char *url) {
   std::string aux(url);
   return XXHash64::hash(url, aux.length(), 0);
}

void Helpers::Nop(struct io_uring *ring, struct Request *request, int timeout) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
   if (timeout > 0) {
      struct __kernel_timespec ts;
      ts.tv_sec = timeout / 1000;
      ts.tv_nsec = (timeout % 1000) * 1000000;
      io_uring_prep_timeout(sqe, &ts, 0, 0);
   } else {
      io_uring_prep_nop(sqe);
   }
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring);
}

void Helpers::CloseFD(int fd) {
   if (fd > 0) {
      close(fd);
   }
}

void Helpers::CleanCache(uint64_t resource_id) {
   std::string path = Settings::CacheDir + std::to_string(resource_id);
   std::string path_header = path + "_h";
   std::remove(path.c_str());
   std::remove(path_header.c_str());
}

long Helpers::GetTicks() {
   auto now = std::chrono::system_clock::now();
   auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
   auto epoch = now_ms.time_since_epoch();
   long epoch_ms = epoch.count();
   return epoch_ms;
}
