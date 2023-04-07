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

   bool AppendBuffer(uint64_t resource_id, void* buffer, int length);
   void CloseBuffer(uint64_t resource_id);

   void AddWriteRequest(struct Request *http);
   int HandleWrite(struct Request *cache);

   bool GenerateNode(struct Request *http);

  private:
   struct Node {
    unsigned int pivot;
    struct Request *cache;
    struct iovec buffer[];
   };

   struct io_uring *ring_;
   Stream *stream_;
   std::unordered_map<uint64_t, struct Node*> nodes_;

   std::string GetCachePath(uint64_t resource_id);
};
#endif
