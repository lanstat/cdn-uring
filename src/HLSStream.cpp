#include "HLSStream.hpp"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

#include "EventType.hpp"
#include "Helpers.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"

#define CHANNEL_LIST_TAG 0x01
#define CHANNEL_TAG 0x02
#define SEGMENT_PLAYLIST_TAG 0x03
#define SEGMENT_TAG 0x04
#define CHANNEL_INNER_TAG 0x05
#define SEGMENT_INNER_PLAYLIST_TAG 0x06

HLSStream::HLSStream() {}

bool HLSStream::HandleExistsResource(struct Request *entry) {
   bool exists = Stream::HandleExistsResource(entry);

   auto mux = GetResource(entry->resource_id);
   if (!exists) {
      char *path = (char *)entry->iov[1].iov_base;
      if (Utils::EndsWith(path, "/playlist.m3u8")) {
         mux->tag = CHANNEL_LIST_TAG;
      } else if (Utils::EndsWith(path, "/index.m3u8")) {
         mux->tag = CHANNEL_TAG;
         mux->in_memory = true;
      } else if (Utils::EndsWith(path, ".m3u8")) {
         mux->tag = SEGMENT_PLAYLIST_TAG;
         mux->in_memory = true;
      } else {
         mux->tag = SEGMENT_TAG;
      }
   } else {
      if (mux->tag == SEGMENT_PLAYLIST_TAG && mux->is_completed) {
         GeneratePlaylist(entry->resource_id, mux);
      }
   }
   return exists;
}

int HLSStream::NotifyCacheCompleted(uint64_t resource_id, struct iovec *buffer,
                                    int size) {
   auto mux = GetResource(resource_id);
   if (mux->tag == CHANNEL_TAG) {
      ProcessChannel(mux, buffer, size);
   } else if (mux->tag == CHANNEL_INNER_TAG) {
      ProcessChannel(mux, buffer, size);
      ReleaseResource(resource_id);
      RemoveSegment(resource_id);
   } else if (mux->tag == SEGMENT_INNER_PLAYLIST_TAG) {
      struct Segment *segment = segments_.at(resource_id);
      ProcessSegmentList(segment, buffer, size);
      ReleaseResource(resource_id);
      RemoveSegment(resource_id);

      // Notify parent segment is completed
      resource_id = segment->segment_parent;
      size = 0;
      auto mux = GetResource(resource_id);
      GeneratePlaylist(resource_id, mux);
   }
   return Stream::NotifyCacheCompleted(resource_id, buffer, size);
}

void HLSStream::ProcessChannel(struct Mux *mux, struct iovec *buffer,
                               int size) {
   struct Request *channel = mux->requests[0];

   for (int i = 0; i < size; i++) {
      if (buffer[i].iov_len == 0) continue;

      std::string content((char *)buffer[i].iov_base);
      std::stringstream stream(content);
      std::string line;
      while (std::getline(stream, line)) {
         if (line.rfind("#EXT-X-STREAM-INF:", 0) == 0) {
            std::string url;
            std::getline(stream, url);
            if (url.rfind("http", 0) == 0) {
               std::string path =
                   url.replace(url.begin(), url.begin() + 7, "/");
               path = path.substr(path.find("/", 1));
               Log(__FILE__, __LINE__) << "Found playlist: " << path;

               uint64_t resource_id = Helpers::GetResourceId(path.c_str());

               if (playlists_.find(resource_id) == playlists_.end()) {
                  CreatePlaylist(resource_id, mux->url);
               }
               RequestFile(resource_id, url, SEGMENT_INNER_PLAYLIST_TAG, 100);
            }
         }
      }

      int pos = content.find(Settings::Proxy);
      if (pos != std::string::npos) {
         while (pos != std::string::npos) {
            content.replace(pos, Settings::Proxy.length(), Settings::BaseUrl);
            pos = content.find(Settings::Proxy);
         }

         memset(buffer[i].iov_base, 0, buffer[i].iov_len);
         buffer[i].iov_len = content.length();
         memcpy(buffer[i].iov_base, (void *)content.c_str(), content.length());
      }
   }
}

void HLSStream::ProcessSegmentList(struct Segment *segment,
                                   struct iovec *buffer, int size) {
   struct SegmentPlaylist *playlist = playlists_.at(segment->segment_parent);
   long epoch_ms = GetTicks();

   bool found = false;
   for (int i = 0; i < size; i++) {
      if (buffer[i].iov_len == 0) continue;
      std::string content((char *)buffer[i].iov_base);
      std::stringstream stream(content);
      std::string line;

      while (std::getline(stream, line)) {
         if (line.rfind("#EXTINF:", 0) == 0) {
            std::string url;
            std::getline(stream, url);
            if (url.rfind("http", 0) == 0) {
               found = true;
               std::string tmp = url;
               tmp = tmp.replace(tmp.begin(), tmp.begin() + 7, "/");
               Log(__FILE__, __LINE__) << "Found TS: " << tmp;

               std::string seconds = line.substr(8, line.find(',') - 8);
               seconds.erase(std::remove(seconds.begin(), seconds.end(), '.'),
                             seconds.end());
               auto stamp = std::stoi(seconds);
               epoch_ms += stamp;

               int pos = url.find(Settings::Proxy);
               url.replace(pos, Settings::Proxy.length(), Settings::BaseUrl);

               playlist->segments.insert(std::pair<long, std::string>(
                   epoch_ms, line + "\n" + url + "\n"));
            }
         }
      }
   }
   if (!found) {
      RequestFile(playlist->resource_id, playlist->url, CHANNEL_INNER_TAG, 500);
   }
}

void HLSStream::CreatePlaylist(uint64_t resource_id, std::string channel_url) {
   auto playlist = new SegmentPlaylist();
   playlist->resource_id = resource_id;
   playlist->url = channel_url;
   playlists_.insert(
       std::pair<uint64_t, struct SegmentPlaylist *>(resource_id, playlist));

   struct Mux *mux = CreateMux(std::string());
   mux->tag = SEGMENT_PLAYLIST_TAG;
   mux->type = RESOURCE_TYPE_CACHE;
   mux->in_memory = true;
   mux->is_completed = false;

   std::stringstream ss;
   ss << "HTTP/1.1 200 OK\r\n"
      << "Server: Astra\r\n"
      << "Cache-Control: no-cache\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Methods: GET\r\n"
      << "Access-Control-Allow-Credentials: true\r\n"
      << "Content-Type: application/vnd.apple.mpegURL\r\n"
      << "Connection: close\r\n"
      << "\r\n";

   std::string header = ss.str();
   int size = header.size() + 1;

   mux->header.iov_len = size;
   mux->header.iov_base = malloc(size);
   memset(mux->header.iov_base, 0, size);
   memcpy(mux->header.iov_base, header.c_str(), size);

   std::pair<uint64_t, struct Mux *> item(resource_id, mux);
   resources_.insert(item);
}

void HLSStream::RequestFile(uint64_t parent_id, std::string url, int tag,
                            int msecs) {
   uint64_t resource_id = Helpers::GetResourceId(url.c_str());
   struct Mux *mux = CreateMux(std::string());
   mux->tag = tag;
   mux->in_memory = true;
   std::pair<uint64_t, struct Mux *> item(resource_id, mux);
   resources_.insert(item);

   struct Segment *segment = new Segment();
   segment->segment_parent = parent_id;
   segment->tag = tag;
   segment->is_last = false;

   segments_.insert(
       std::pair<uint64_t, struct Segment *>(resource_id, segment));

   auto inner = Utils::CreateRequest(5);
   inner->resource_id = resource_id;

   int size = url.size() + 1;
   inner->iov[2].iov_len = size;
   inner->iov[2].iov_base = malloc(size);
   memset(inner->iov[2].iov_base, 0, size);
   memcpy(inner->iov[2].iov_base, url.c_str(), size);

   std::stringstream ss;
   ss << "User-Agent: Lavf/60.3.100\r\n"
      << "Connection: keep-alive\r\n"
      << "Ranges: bytes=0-\r\n"
      << "Icy-MetaData: 1\r\n"
      << "Host: 200.58.170.69:58001\r\n"
      << "Accept: */*\r\n";

   std::string header = ss.str();
   size = header.size() + 1;

   inner->iov[3].iov_len = size;
   inner->iov[3].iov_base = malloc(size);
   memset(inner->iov[3].iov_base, 0, size);
   memcpy(inner->iov[3].iov_base, header.c_str(), size);

   inner->event_type = EVENT_TYPE_DNS_FETCHAAAA;
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   if (msecs > 0) {
      struct __kernel_timespec ts;
      ts.tv_sec = msecs / 1000;
      ts.tv_nsec = (msecs % 1000) * 1000000;
      io_uring_prep_timeout(sqe, &ts, 0, 0);
   } else {
      io_uring_prep_nop(sqe);
   }
   io_uring_sqe_set_data(sqe, inner);
   io_uring_submit(ring_);
}

void HLSStream::RemoveSegment(uint64_t resource_id) {}

long HLSStream::GetTicks() {
   auto now = std::chrono::system_clock::now();
   auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
   auto epoch = now_ms.time_since_epoch();
   long epoch_ms = epoch.count();
   return epoch_ms;
}

void HLSStream::GeneratePlaylist(uint64_t resource_id, struct Mux *mux) {
   std::stringstream ss;
   ss << "#EXT-X-VERSION:3\r\n";
   ss << "#EXT-X-TARGETDURATION:3\r\n";
   ss << "#EXT-X-MEDIA-SEQUENCE:2086\r\n";

   struct SegmentPlaylist *playlist = playlists_.at(resource_id);
   for (auto segment : playlist->segments) {
      ss << segment.second;
   }
   std::string content = ss.str();
   int size = content.size() + 1;

   mux->buffer[0].iov_len = size;
   mux->buffer[0].iov_base = malloc(size);
   memset(mux->buffer[0].iov_base, 0, size);
   memcpy(mux->buffer[0].iov_base, content.c_str(), size);
   std::cout << "LAN_[" << __FILE__ << ":" << __LINE__ << "] "
             << "generated" << std::endl;
}
