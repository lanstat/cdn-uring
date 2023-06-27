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

   void SetCacheResource(uint64_t resource_id, const std::string &header_data,
                         std::string path, bool is_completed);
   void SetStreamingResource(uint64_t resource_id,
                             const std::string &header_data);

   void AddWriteHeaders(struct Request *stream, struct Mux *mux);
   void HandleWriteHeaders(struct Request *stream);

   int AddWriteStreamRequest(struct Request *stream);

   bool HandleExistsResource(struct Request *entry);
   void AbortStream(uint64_t resource_id);
   void CloseStream(uint64_t resource_id);

   int NotifyStream(uint64_t resource_id, void *buffer, int size);

   int NotifyCache(uint64_t resource_id, bool is_completed);
   int AddWriteFromCacheRequest(struct Request *stream, struct Mux *mux);
   bool HandleReadCacheRequest(struct Request *stream, int readed);
   int HandleCopyCacheRequest(struct Request *stream, int readed);

   int RemoveRequest(struct Request *request);
   void ReleaseErrorAllWaitingRequest(uint64_t resource_id, int status_code);

   struct Mux *GetResource(uint64_t resource_id);
   bool ExistsResource(uint64_t resource_id);

  private:
   struct io_uring *ring_;
   Server *server_;
   std::unordered_map<uint64_t, struct Mux *> resources_;

   struct Mux *CreateMux();
   void ReleaseResource(uint64_t resource_id);
};
#endif

