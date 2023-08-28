#include "Http.hpp"

#pragma GCC diagnostic ignored "-Wpointer-arith"

#include <arpa/inet.h>
#include <string.h>

#include <chrono>
#include <ctime>
#include <sstream>
#include <vector>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"

#define ZERO_LENGTH 1

const std::string GetEtag(uint64_t resource_id) {
   time_t now = time(0);
   struct tm tstruct;
   char buf[80];
   tstruct = *localtime(&now);
   strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tstruct);
   std::string stamp(buf);

   return "\"" + std::to_string(resource_id) + stamp + "\"";
}

Http::Http() {
   stream_ = nullptr;
   cache_ = nullptr;

   buffer_size_ = Settings::HttpBufferSize;

   zero_ = malloc(ZERO_LENGTH);
   memset(zero_, 0, ZERO_LENGTH);
}

Http::~Http() { free(zero_); }

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::SetCache(Cache *cache) { cache_ = cache; }

void Http::SetStream(Stream *stream) { stream_ = stream; }

void Http::AddFetchDataRequest(struct Request *req) {}

int Http::GetResourceType(char *header, int size) {
   auto connection = Utils::GetHeaderTag(header, "Connection");
   if (connection == "close") {
      return RESOURCE_TYPE_CACHE;
   }
   return RESOURCE_TYPE_STREAMING;
}

int Http::FetchHeaderLength(char *header, int size) {
   int offset = 0;
   while (offset < size) {
      if (header[offset] == '\r' && header[offset + 1] == '\n' &&
          header[offset + 2] == '\r' && header[offset + 3] == '\n') {
         offset += 4;
         break;
      }
      offset++;
   }
   return offset;
}

std::string Http::GetExternalHeader(char *header) {
   std::string tmp(header);

   if (!Settings::AstraMode) {
      tmp = Utils::ReplaceHeaderTag(tmp, "Connection", "close");
   }
   return tmp + "\r\n";
}

std::string Http::ProcessExternalHeader(struct Request *http) {
   int size = http->iov[0].iov_len + 1;
   void *header = malloc(size);
   memset(header, 0, size);
   memcpy(header, http->iov[0].iov_base, size - 1);
   std::string tmp((char *)header);
   free(header);

   // tmp = Utils::ReplaceHeaderTag(tmp, "Server", "cdn/0.1.0");
   //  tmp = Utils::ReplaceHeaderTag(tmp, "ETag", GetEtag(http->resource_id));

   return tmp;
}

int Http::HandleReadHeaderRequest(struct Request *http, int readed) {
   readed = PreRequest(http, readed);
   if (readed <= 0) {
      if (readed < 0) {
         stream_->ReleaseErrorAllWaitingRequest(http->resource_id, 502);
      } else {
         stream_->CloseStream(http->resource_id);
      }
      ReleaseSocket(http);
      return 1;
   }

   int type = GetResourceType((char *)http->iov[0].iov_base, readed);
   int header_length = FetchHeaderLength((char *)http->iov[0].iov_base, readed);
   http->iov[0].iov_len = header_length;
   std::string new_header = ProcessExternalHeader(http);

   if (type == RESOURCE_TYPE_CACHE) {
      http->iov[0].iov_len = header_length;
      cache_->GenerateNode(http, new_header);

      if (header_length < readed) {
         cache_->AppendBuffer(http->resource_id,
                              http->iov[0].iov_base + header_length,
                              readed - header_length);
      }
   } else if (type == RESOURCE_TYPE_STREAMING) {
      http->iov[0].iov_len = readed;
      stream_->SetStreamingResource(http->resource_id, new_header);
      if (header_length < readed) {
         stream_->NotifyStream(http->resource_id,
                               http->iov[0].iov_base + header_length,
                               readed - header_length);
      }
   }
   http->pivot = type;
   http->event_type = EVENT_TYPE_HTTP_READ_CONTENT;

   PostRequest(http);

   return 0;
}

int Http::HandleReadData(struct Request *http, int readed) {
   int type = http->pivot;
   readed = PreRequest(http, readed);
   if (readed <= 0) {
      if (readed < 0) {
         stream_->ReleaseErrorAllWaitingRequest(http->resource_id, 502);
      } else {
         if (type == RESOURCE_TYPE_CACHE) {
            cache_->CloseBuffer(http->resource_id);
         } else if (type == RESOURCE_TYPE_STREAMING) {
            stream_->CloseStream(http->resource_id);
         }
      }
      ReleaseSocket(http);
      return 1;
   }

   if (type == RESOURCE_TYPE_CACHE) {
      cache_->AppendBuffer(http->resource_id, http->iov[0].iov_base, readed);
   } else if (type == RESOURCE_TYPE_STREAMING) {
      // If there is no listeners
      if (stream_->NotifyStream(http->resource_id, http->iov[0].iov_base,
                                readed) == 1) {
         Log(__FILE__, __LINE__) << "HttpClient empty stream listeners";
         ReleaseSocket(http);
         return 1;
      }
   }

   memset(http->iov[0].iov_base, 0, buffer_size_);

   PostRequest(http);

   return 0;
}

int Http::GetStatusCode(std::string header) {
   std::string status_code = header.substr(9, 3);
   return std::stoi(status_code);
}

void Http::Retry(struct Request *request, bool ipv4) {
   Log(__FILE__, __LINE__, Log::kWarning) << "Retry " << request->resource_id;
   int msecs = 200;

   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   struct __kernel_timespec ts;
   ts.tv_sec = msecs / 1000;
   ts.tv_nsec = (msecs % 1000) * 1000000;
   io_uring_prep_timeout(sqe, &ts, 0, 0);
   io_uring_sqe_set_data(sqe, request);
   io_uring_submit(ring_);
}
