#include "AstraHttpClient.hpp"

#pragma GCC diagnostic ignored "-Wpointer-arith"

#include <string.h>

#include <sstream>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

#define HTTP_TYPE_INDEX 1
#define HTTP_TYPE_CONTENT 2
#define HTTP_TYPE_TS 3

#define TS_TIMEOUT 1250

AstraHttpClient::AstraHttpClient() {}

int AstraHttpClient::HandleReadHeaderRequest(struct Request *http, int readed) {
   auto resource = stream_->GetResource(http->resource_id);

   readed = PreRequest(http, readed);
   if (readed <= 0) {
      ReleaseSocket(http);
      return 1;
   }

   int header_length = FetchHeaderLength((char *)http->iov[0].iov_base, readed);
   http->iov[0].iov_len = header_length;
   std::string new_header = ProcessExternalHeader(http);

   if (resource->type == RESOURCE_TYPE_UNDEFINED) {
      stream_->SetStreamingResource(http->resource_id, new_header);
   }

   char *url = (char *)http->iov[1].iov_base;
   if (Utils::EndsWith(url, "/index.m3u8")) {
      std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< url << std::endl;
      http->pivot = HTTP_TYPE_INDEX;
      if (playlist_.find(http->resource_id) == playlist_.end()) {
         CreatePlaylist(http);
      }
   } else if (Utils::EndsWith(url, ".ts")) {
      http->pivot = HTTP_TYPE_TS;
   } else {
      http->pivot = HTTP_TYPE_CONTENT;
   }

   if (VerifyContent(http)) {
      return 0;
   }

   http->event_type = EVENT_TYPE_HTTP_READ_CONTENT;
   memset(http->iov[0].iov_base, 0, buffer_size_);

   PostRequest(http);

   return 0;
}

int AstraHttpClient::HandleReadData(struct Request *http, int readed) {
   int type = http->pivot;
   readed = PreRequest(http, readed);
   if (readed <= 0) {
      if (readed < 0) {
         stream_->ReleaseErrorAllWaitingRequest(http->resource_id, 502);
      } else if (!PlayNextTrack(http)) {
         auto playlist = playlist_.at(http->resource_id);
         RequestTrack(http, playlist->url, TS_TIMEOUT);
      }
      ReleaseSocket(http);
      return 1;
   }

   if (VerifyContent(http)) {
      return 0;
   }

   if (stream_->NotifyStream(http->resource_id, http->iov[0].iov_base,
                             readed) == 1) {
      Log(__FILE__, __LINE__) << "HttpClient empty stream listeners";
      ReleasePlaylist(http);
      ReleaseSocket(http);
      return 1;
   }

   memset(http->iov[0].iov_base, 0, buffer_size_);

   PostRequest(http);

   return 0;
}

bool AstraHttpClient::VerifyContent(struct Request *http) {
   if (http->pivot == HTTP_TYPE_INDEX) {
      if (ProcessPlaylist(http)) {
         ReleaseSocket(http);
         return true;
      }
   } else if (http->pivot == HTTP_TYPE_CONTENT) {
      if (ProcessReproduction(http)) {
         ReleaseSocket(http);
         return true;
      }
   }

   return false;
}

bool AstraHttpClient::ProcessReproduction(struct Request *http) {
   std::string content((char *)http->iov[0].iov_base);
   std::stringstream stream(content);
   std::string line;
   while (std::getline(stream, line)) {
      if (line.rfind("#EXTINF:", 0) == 0) {
         std::string url;
         std::getline(stream, url);
         if (url.rfind("http", 0) == 0) {
            url = url.replace(url.begin(), url.begin() + 7, "/");
            Log(__FILE__, __LINE__, Log::kDebug) << "Found TS: " << url;
            playlist_.at(http->resource_id)->tracks.push_back(url);
         }
      }
   }

   return PlayNextTrack(http, true);
}

bool AstraHttpClient::PlayNextTrack(struct Request *http, bool is_first) {
   if (!playlist_.at(http->resource_id)->tracks.empty()) {
      std::string url = playlist_.at(http->resource_id)->tracks.front();
      playlist_.at(http->resource_id)
          ->tracks.erase(playlist_.at(http->resource_id)->tracks.begin());
      if (is_first) {
         RequestTrack(http, url);
      } else {
         RequestTrack(http, url, TS_TIMEOUT);
      }
      return true;
   }

   return false;
}

bool AstraHttpClient::ProcessPlaylist(struct Request *http) {
   std::string content((char *)http->iov[0].iov_base);
   std::stringstream stream(content);
   std::string line;
   while (std::getline(stream, line)) {
      if (line.rfind("#EXT-X-STREAM-INF:", 0) == 0) {
         std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< line << std::endl;
         std::string url;
         std::getline(stream, url);
         if (url.rfind("http", 0) == 0) {
            url = url.replace(url.begin(), url.begin() + 7, "/");
            Log(__FILE__, __LINE__, Log::kDebug) << "Found playlist: " << url;
            if (playlist_.at(http->resource_id)->url.empty()) {
               playlist_.at(http->resource_id)->url = url;
               RequestTrack(http, url);
            } else {
               RequestTrack(http, url, TS_TIMEOUT);
            }
            return true;
         }
      }
   }
   return false;
}

void AstraHttpClient::ReleaseSocket(struct Request *http) {
   HttpClient::ReleaseSocket(http);
}

void AstraHttpClient::RequestTrack(struct Request *http, std::string url, int msecs) {
   struct Request *inner = Utils::InnerRequest(http);

   int size = url.size() + 1;
   inner->iov[2].iov_len = size;
   inner->iov[2].iov_base = malloc(size);
   memset(inner->iov[2].iov_base, 0, size);
   memcpy(inner->iov[2].iov_base, url.c_str(), size);

   std::stringstream ss;
   ss << "User-Agent: Lavf/60.3.100\r\n"
      << "Range: bytes=0-\r\n"
      << "Connection: keep-alive\r\n"
      << "Icy-MetaData: 1\r\n"
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

void AstraHttpClient::ReleasePlaylist(struct Request *http) {
   auto it = playlist_.at(http->resource_id);
   delete it;
   playlist_.erase(http->resource_id);
}

void AstraHttpClient::CreatePlaylist(struct Request *http) {
   struct Playlist *playlist = new Playlist();
   std::vector<std::string> tracks;

   playlist->tracks = tracks;
   std::pair<uint64_t, struct Playlist *> item(http->resource_id, playlist);
   playlist_.insert(item);
}
