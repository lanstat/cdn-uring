#include "Helpers.hpp"

#include <chrono>

#include "xxhash64.h"

uint64_t Helpers::GetResourceId(const char *url) {
   std::string aux(url);
   return XXHash64::hash(url, aux.length(), 0);
}

void Helpers::SendRequestNop(struct io_uring *ring, struct Request *request,
                             int timeout) {
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
