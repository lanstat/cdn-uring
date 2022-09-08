#include "Dns.hpp"

#include <string.h>
#include <time.h>

#include "EventType.hpp"
#include "Request.hpp"

#define VERIFY_TIMEOUT 50000000  // 50 ms

Dns::Dns() {}

void Dns::SetRing(struct io_uring *ring) { ring_ = ring; }

int Dns::HandleVerifyUDP() { return 0; }

void Dns::AddVerifyUDPRequest() {
   struct timespec rts;
   int ret = clock_gettime(CLOCK_MONOTONIC, &rts);
   if (ret < 0) {
      fprintf(stderr, "clock_gettime CLOCK_REALTIME error: %s\n",
              strerror(errno));
   }
   rts.tv_nsec += VERIFY_TIMEOUT;
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   struct Request *req = (Request *)malloc(sizeof(*req) + sizeof(struct iovec));
   req->event_type = EVENT_TYPE_DNS_VERIFY;
   io_uring_prep_timeout(sqe, (struct __kernel_timespec *)&rts, 1,
                         IORING_TIMEOUT_ABS);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);
}

void Dns::AddFetchAAAARequest(struct Request *request) {}

int Dns::HandleFetchAAAA(struct Request *request) { return 0; }
