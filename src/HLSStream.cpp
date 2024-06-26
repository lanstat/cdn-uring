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

#define REMAINING_FETCHES 10
#define MAX_TRACKS_PER_PLAYLIST 4

#define TIMEOUT_TRACK 15000
#define TRACK_DELTA 150

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
         mux->transfer_mode = TRANSFER_MODE_WAIT_UNTIL_COMPLETE;
         mux->in_memory = true;
      } else if (Utils::EndsWith(path, ".m3u8")) {
         mux->tag = SEGMENT_PLAYLIST_TAG;
         mux->in_memory = true;
      } else {
         mux->tag = SEGMENT_TAG;
      }
   } else {
      if (mux->tag == CHANNEL_TAG) {
      } else if (mux->tag == SEGMENT_PLAYLIST_TAG) {
         struct SegmentPlaylist *playlist = playlists_.at(entry->resource_id);
         playlist->remaining_fetches = REMAINING_FETCHES;
         if (mux->is_completed) {
            GenerateSegmentClientList(entry->resource_id, mux);
         }
      }
   }
   return exists;
}

int HLSStream::NotifyCacheCompleted(uint64_t resource_id, struct iovec *buffer,
                                    int size) {
   auto mux = GetResource(resource_id);
   if (mux->tag == SEGMENT_TAG) {
      AddResourceTTL(resource_id, TIMEOUT_TRACK);
      return Stream::NotifyCacheCompleted(resource_id, buffer, size);
   }

   if (mux->tag == SEGMENT_INNER_PLAYLIST_TAG) {
      uint64_t parent_id = segments_.at(resource_id);
      bool proceed = ProcessSegmentList(parent_id, buffer, size);
      RemoveSegment(resource_id);

      if (proceed) {
         // Notify parent segment is completed
         resource_id = parent_id;
         size = 0;
         auto mux = GetResource(resource_id);
         GenerateSegmentClientList(resource_id, mux);
      } else {
         return 1;
      }
   }

   struct iovec *new_buffer = new struct iovec[size];
   if (mux->tag == CHANNEL_TAG) {
      ProcessChannel(mux, buffer, size, 0, new_buffer);
   } else if (mux->tag == CHANNEL_INNER_TAG) {
      uint64_t parent_id = segments_.at(resource_id);
      ProcessChannel(mux, buffer, size, parent_id, new_buffer);
      RemoveSegment(resource_id);
   }
   int response = Stream::NotifyCacheCompleted(resource_id, new_buffer, size);

   for (int i = 0; i < size; i++) {
      if (new_buffer[i].iov_len > 0) {
         free(new_buffer[i].iov_base);
      }
   }

   delete[] new_buffer;

   return response;
}

void HLSStream::ProcessChannel(struct Mux *mux, struct iovec *buffer, int size,
                               uint64_t parent_id, struct iovec *new_buffer) {
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
               Log(__FILE__, __LINE__, Log::kDebug)
                   << "Found playlist: " << path;

               uint64_t resource_id = parent_id;

               // Create playlist is the request is triggered by user
               if (parent_id == 0) {
                  resource_id = Helpers::GetResourceId(path.c_str());
                  if (playlists_.find(resource_id) == playlists_.end()) {
                     CreatePlaylist(resource_id, mux->resource_id, mux->url);
                  }
               }
               RequestFile(resource_id, url, SEGMENT_INNER_PLAYLIST_TAG, 100);
               break;
            }
         }
      }

      int pos = content.find(Settings::Proxy);
      if (pos != std::string::npos) {
         while (pos != std::string::npos) {
            content.replace(pos, Settings::Proxy.length(), Settings::BaseUrl);
            pos = content.find(Settings::Proxy);
         }

         new_buffer[i].iov_len = content.length();
         new_buffer[i].iov_base = malloc(new_buffer[i].iov_len);
         memset(new_buffer[i].iov_base, 0, new_buffer[i].iov_len);
         memcpy(new_buffer[i].iov_base, (void *)content.c_str(),
                content.length());

         UpdateHeader(mux, content.length());
      }
   }
}

bool HLSStream::ProcessSegmentList(uint64_t parent_id, struct iovec *buffer,
                                   int size) {
   struct SegmentPlaylist *playlist = playlists_.at(parent_id);

   if (playlist->remaining_fetches <= 0) {
      RemovePlaylist(parent_id);
      return false;
   }
   playlist->remaining_fetches--;

   long last_stamp = playlist->last_stamp;
   if (last_stamp == 0) {
      last_stamp = Helpers::GetTicks() + TRACK_DELTA;
   }
   int sequence = 0;
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
               std::string seconds = line.substr(8, line.find(',') - 8);
               seconds.erase(std::remove(seconds.begin(), seconds.end(), '.'),
                             seconds.end());
               auto stamp = std::stoi(seconds);
               uint64_t uid = Helpers::GetResourceId(url.c_str());
               sequence++;

               if (std::find(playlist->track_uids.begin(),
                             playlist->track_uids.end(),
                             uid) != playlist->track_uids.end()) {
                  continue;
               }

               std::string tmp = url;

               tmp = tmp.replace(tmp.begin(), tmp.begin() + 7, "/");
               //Log(__FILE__, __LINE__) << "Found TS: " << tmp;
               Log(__FILE__, __LINE__) << "Found TS: " << last_stamp;

               int pos = url.find(Settings::Proxy);
               if (pos != std::string::npos) {
                  url.replace(pos, Settings::Proxy.length(), Settings::BaseUrl);
               }

               std::string block = line + "\n" + url + "\n";
               playlist->track_stamps.push_back(last_stamp);
               playlist->track_uids.push_back(uid);
               playlist->track_urls.push_back(block);
               playlist->track_sequence.push_back(sequence);

               last_stamp += stamp;
            }
         } else if (line.rfind("#EXT-X-MEDIA-SEQUENCE:", 0) == 0) {
            std::string tmp = line.substr(22);
            sequence = std::stoi(tmp);
            sequence--;
            continue;
         }
      }
   }

   if (playlist->last_stamp != last_stamp) {
      playlist->last_stamp = last_stamp;
      CleanTracks(playlist);
   }

   if (!found) {
      RequestFile(playlist->resource_id, playlist->url, CHANNEL_INNER_TAG, 500);
   } else {
      RequestFile(playlist->resource_id, playlist->url, CHANNEL_INNER_TAG,
                  2500);
   }
   return true;
}

void HLSStream::CreatePlaylist(uint64_t resource_id, uint64_t channel_id,
                               std::string channel_url) {
   auto playlist = new SegmentPlaylist();
   playlist->resource_id = resource_id;
   playlist->channel_id = channel_id;
   playlist->url = channel_url;
   playlist->remaining_fetches = REMAINING_FETCHES;
   playlist->last_stamp = 0;
   playlists_.insert(
       std::pair<uint64_t, struct SegmentPlaylist *>(resource_id, playlist));

   struct Mux *mux = CreateMux(resource_id, std::string());
   mux->tag = SEGMENT_PLAYLIST_TAG;
   mux->type = RESOURCE_TYPE_CACHE;
   mux->transfer_mode = TRANSFER_MODE_WAIT_UNTIL_COMPLETE;
   mux->in_memory = true;
   mux->is_completed = false;

   std::stringstream ss;
   ss << "HTTP/1.1 200 OK\r\n"
      << "Server: Astra\r\n"
      << "Cache-Control: no-cache\r\n"
      << "Content-Length: 0\r\n"
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
   struct Mux *mux = CreateMux(resource_id, std::string());
   mux->tag = tag;
   mux->in_memory = true;
   std::pair<uint64_t, struct Mux *> item(resource_id, mux);
   resources_.insert(item);

   CreateSegment(resource_id, parent_id);

   auto inner = Utils::CreateRequest(5);
   inner->resource_id = resource_id;
   inner->event_type = EVENT_TYPE_DNS_FETCHAAAA;

   auto stream = Utils::StreamRequest(inner);
   // Prevent print the request on the console :P
   stream->is_processing = true;
   mux->requests.push_back(stream);

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
      << "Accept: */*\r\n";

   std::string header = ss.str();
   size = header.size() + 1;

   inner->iov[3].iov_len = size;
   inner->iov[3].iov_base = malloc(size);
   memset(inner->iov[3].iov_base, 0, size);
   memcpy(inner->iov[3].iov_base, header.c_str(), size);

   Helpers::Nop(ring_, inner, msecs);
}

void HLSStream::GenerateSegmentClientList(uint64_t resource_id,
                                          struct Mux *mux) {
   // Only generate playlist if there are pending requests
   if (mux->requests.empty()) {
      return;
   }
   struct SegmentPlaylist *playlist = playlists_.at(resource_id);
   long ticks = Helpers::GetTicks();

   std::stringstream ss;
   ss << "#EXTM3U\r\n";
   ss << "#EXT-X-VERSION:3\r\n";
   ss << "#EXT-X-TARGETDURATION:3\r\n";

   // Only copy the tracks that are after the current ticks
   int pivot = 0;
   const std::vector<long> &stamps = playlist->track_stamps;
   for (int i = 0; i < stamps.size(); i++) {
      if (stamps[i] > ticks) {
         pivot = i;
         // if (pivot > 0) {
         // pivot--;
         //}
         break;
      }
   }
   ss << "#EXT-X-MEDIA-SEQUENCE:" << playlist->track_sequence.at(pivot)
      << "\r\n";
   const std::vector<std::string> &urls = playlist->track_urls;
   for (int i = 0; i < MAX_TRACKS_PER_PLAYLIST; i++) {
      if ((pivot + i) >= urls.size()) {
         break;
      }
      ss << urls.at(pivot + i);
   }
   std::string content = ss.str();
   int size = content.size();

   mux->buffer[0].iov_len = size;
   mux->buffer[0].iov_base = malloc(size);
   memset(mux->buffer[0].iov_base, 0, size);
   memcpy(mux->buffer[0].iov_base, content.c_str(), size);

   UpdateHeader(mux, size);
}

void HLSStream::CreateSegment(uint64_t resource_id, uint64_t parent) {
   segments_.insert(std::pair<uint64_t, uint64_t>(resource_id, parent));
}

void HLSStream::RemovePlaylist(uint64_t resource_id) {
   Log(__FILE__, __LINE__, Log::kWarning)
       << "Removing playlist " << resource_id;
   auto playlist = playlists_.at(resource_id);

   if (ExistsResource(playlist->channel_id)) {
      auto channel_mux = GetResource(playlist->channel_id);
      const std::vector<struct Request *> &requests = channel_mux->requests;
      for (struct Request *const c : requests) {
         Utils::ReleaseRequest(c);
      }
      ReleaseResource(channel_mux->resource_id);
   }

   delete playlist;
   playlists_.erase(resource_id);
}

void HLSStream::RemoveSegment(uint64_t resource_id) {
   auto mux = GetResource(resource_id);
   const std::vector<struct Request *> &requests = mux->requests;
   for (struct Request *const c : requests) {
      Utils::ReleaseRequest(c);
   }
   ReleaseResource(resource_id);

   segments_.erase(resource_id);
}

void HLSStream::UpdateHeader(struct Mux *mux, int content_length) {
   std::string header((char *)mux->header.iov_base);

   header = Utils::ReplaceHeaderTag(header, "Content-Length",
                                    std::to_string(content_length));
   if (mux->header.iov_len > 0) {
      free(mux->header.iov_base);
   }
   mux->header.iov_len = header.size();
   mux->header.iov_base = malloc(mux->header.iov_len);
   memset(mux->header.iov_base, 0, mux->header.iov_len);
   memcpy(mux->header.iov_base, (void *)header.c_str(), header.size());
}

void HLSStream::CleanTracks(struct SegmentPlaylist *playlist) {
   if (playlist->track_urls.size() < 10) {
      return;
   }

   int count = playlist->track_urls.size() - 10;
   std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< count << std::endl;

   playlist->track_urls.erase(playlist->track_urls.begin(),
                              playlist->track_urls.begin() + count);
   playlist->track_uids.erase(playlist->track_uids.begin(),
                              playlist->track_uids.begin() + count);
   playlist->track_stamps.erase(playlist->track_stamps.begin(),
                                playlist->track_stamps.begin() + count);
   playlist->track_sequence.erase(playlist->track_sequence.begin(),
                                  playlist->track_sequence.begin() + count);
}
