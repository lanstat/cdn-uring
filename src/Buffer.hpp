#ifndef SRC_BUFFER_HPP_
#define SRC_BUFFER_HPP_

#include <liburing.h>

#include <vector>

#include "Request.hpp"

class Buffer {
  public:
   Buffer();
   ~Buffer();
   struct iovec *iov_;
   void SetRing(struct io_uring *ring);

   struct Request *HttpEntryRequest();
   struct Request *StreamRequest(struct Request *entry);
   struct Request *InnerRequest(struct Request *entry);
   struct Request *CacheRequest(struct Request *entry);
   struct Request *HttpErrorRequest();
   struct Request *HttpExternalRequest(struct Request *cache);
   struct Request *HttpsExternalRequest(struct Request *inner);
   void ReleaseRequest(struct Request *request);
   void DeFragmentationTable();
   void ReserveMemory();

  private:
   std::vector<struct Request *> requests_;
   struct io_uring *ring_;

   struct Request *CreateRequest(int iovec_count);
   struct Request *AssignRequest(int iovec_count);
};
#endif
