#ifndef SRC_CACHE_HPP_
#define SRC_CACHE_HPP_

#include <liburing.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mux.hpp"
#include "Request.hpp"
#include "Stream.hpp"

class Cache {
  public:
   Cache();

   void SetRing(struct io_uring *ring);
   void SetStream(Stream *stream);

   void AddVerifyRequest();
   int HandleVerify();

   void AddExistsRequest(struct Request *entry);
   bool HandleExists(struct Request *request);

   void AddReadRequest(struct Request *request);
   int HandleRead(struct Request *request);

   void AddReadHeaderRequest(struct Request *cache);
   int HandleReadHeader(struct Request *cache, int readed);

   void AddWriteRequest(struct Request *request);
   int HandleWrite(struct Request *request);

   /*
   void CloseStream(uint64_t resource_id);
   int AddWriteRequestStream(uint64_t resource_id, void *buffer, int size);
   int AddWriteRequestStream(struct Request *request);
   int HandleWriteStream(struct Request *request);

   void ReleaseErrorAllWaitingRequest(struct Request *request, int status_code);
   void ReleaseAllWaitingRequest(struct Request *request);

   int RemoveRequest(struct Request *request);
   */

  private:
   struct io_uring *ring_;
   Stream *stream_;

   std::string GetCachePath(uint64_t resource_id);
};
#endif
