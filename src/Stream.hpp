#ifndef SRC_STREAM_HPP_
#define SRC_STREAM_HPP_

#include "Mux.hpp"
#include "Request.hpp"
#include "Server.hpp"

class Stream {
  public:
   Stream();

   void SetRing(struct io_uring *ring);
   void SetServer(Server *server);

   void SetCacheResource(uint64_t resource_id, struct Request *cache, std::string path);
   void SetStreamingResource(uint64_t resource_id, struct Request *http);

   void AddWriteHeaders(struct Request *stream, struct Mux *mux);
   void HandleWriteHeaders(struct Request *stream);

   int AddWriteStreamRequest(struct Request *stream);
   int AddWriteCacheRequest(struct Request *stream);

   bool HandleExistsResource(struct Request *entry);
   void AbortStream(uint64_t resource_id);
   void CloseStream(uint64_t resource_id);

   int NotifyStream(uint64_t resource_id, void *buffer, int size);

   int RemoveRequest(struct Request *request);
   void ReleaseErrorAllWaitingRequest(uint64_t resource_id, int status_code);

  private:
   struct io_uring *ring_;
   Server *server_;
   std::unordered_map<uint64_t, struct Mux *> resources_;

   struct Mux *CreateMux();
   void ReleaseResource(uint64_t resource_id);
};
#endif

