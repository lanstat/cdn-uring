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
      std::string url;
      std::map<long, std::string> segments;
   };

   struct Segment {
      uint64_t segment_parent;
      int tag;
      bool is_last;
   };

   std::unordered_map<uint64_t, struct SegmentPlaylist *> playlists_;
   std::unordered_map<uint64_t, struct Segment *> segments_;

  private:
   void ProcessChannel(struct Mux *mux, struct iovec *buffer, int size);
   void ProcessSegmentList(struct Segment *segment, struct iovec *buffer, int size);
   void CreatePlaylist(uint64_t resource_id, std::string channel_url);
   void AppendSegments();
   void RequestPlaylist(uint64_t resource_id, std::string url);
   void RequestFile(uint64_t parent_id, std::string url, int tag, int msecs = 0);
   void RemoveSegment(uint64_t resource_id);

   long GetTicks();
   void GeneratePlaylist(struct Mux *mux);

};
#endif

