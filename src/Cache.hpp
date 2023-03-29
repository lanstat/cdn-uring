#ifndef SRC_CACHE_HPP_
#define SRC_CACHE_HPP_

#include <liburing.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Mux.hpp"
#include "Request.hpp"
#include "Server.hpp"

class Cache {
  public:
   Cache();

   void SetRing(struct io_uring *ring);
   void SetServer(Server *server);

   void AddVerifyRequest();
   int HandleVerify();

   void AddExistsRequest(struct Request *request);
   int HandleExists(struct Request *request);

   void AddReadRequest(struct Request *request);
   int HandleRead(struct Request *request);

   void AddWriteRequest(struct Request *request);
   int HandleWrite(struct Request *request);

   void CloseStream(uint64_t resource_id);
   int AddWriteRequestStream(uint64_t resource_id, void *buffer, int size);
   int AddWriteRequestStream(struct Request *request);
   int HandleWriteStream(struct Request *request);

   void ReleaseErrorAllWaitingRequest(struct Request *request, int status_code);
   void ReleaseAllWaitingRequest(struct Request *request);

   int RemoveRequest(struct Request *request);

  private:
   struct io_uring *ring_;
   Server *server_;

   std::string GetUID(uint64_t resource_id);
   uint64_t GetResourceId(char *url);
   std::unordered_map<std::string, struct File> files_;
   std::unordered_map<uint64_t, struct Mux *> waiting_read_;

   void AddCopyRequest(struct Request *request, File *file);
   void StoreFileInMemory(struct Request *request);
   void ReleaseResource(uint64_t resource_id);

   struct Mux *CreateMux();
};
#endif
