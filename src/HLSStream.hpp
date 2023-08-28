#ifndef SRC_HLSSTREAM_HPP_
#define SRC_HLSSTREAM_HPP_

#include <map>
#include <unordered_map>

#include "Stream.hpp"

class HLSStream : public Stream {
  public:
   HLSStream();
   bool HandleExistsResource(struct Request *entry) override;
   int NotifyCacheCompleted(uint64_t resource_id, struct iovec *buffer,
                            int size) override;

  protected:
   struct SegmentPlaylist {
      uint64_t resource_id;
      uint64_t channel_id;
      int remaining_fetches;
      int sequence;
      std::string url;
      std::vector<std::string> track_urls;
      std::vector<uint64_t> track_uids;
      std::vector<long> track_stamps;
   };

   std::unordered_map<uint64_t, struct SegmentPlaylist *> playlists_;
   std::unordered_map<uint64_t, uint64_t> segments_;

  private:
   void ProcessChannel(struct Mux *mux, struct iovec *buffer, int size, uint64_t parent_id, struct iovec *new_buffer);
   bool ProcessSegmentList(uint64_t parent_id, struct iovec *buffer, int size);
   void CreatePlaylist(uint64_t resource_id, uint64_t channel_id, std::string channel_url);
   void AppendSegments();
   void RequestPlaylist(uint64_t resource_id, std::string url);
   void RequestFile(uint64_t parent_id, std::string url, int tag, int msecs = 0);

   void GenerateSegmentClientList(uint64_t resource_id, struct Mux *mux);
   void CreateSegment(uint64_t resource_id, uint64_t parent);

   void RemoveSegment(uint64_t resource_id);
   void RemovePlaylist(uint64_t resource_id);
   void UpdateHeader(struct Mux *mux, int content_length);

};
#endif

