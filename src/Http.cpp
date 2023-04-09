#include "Http.hpp"

#pragma GCC diagnostic ignored "-Wpointer-arith"

#include <arpa/inet.h>
#include <string.h>

#include <sstream>
#include <vector>

#include "EventType.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"

#define ZERO_LENGTH 1

Http::Http() {
   stream_ = nullptr;
   cache_ = nullptr;

   buffer_size_ = Settings::HttpBufferSize;

   zero_ = malloc(ZERO_LENGTH);
   memset(zero_, 0, ZERO_LENGTH);
}

Http::~Http() {
   free(zero_);
}

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::SetCache(Cache *cache) { cache_ = cache; }

void Http::SetStream(Stream *stream) { stream_ = stream; }

void Http::AddFetchDataRequest(struct Request *req) {}

int Http::GetResourceType(char *header, int size) {
   // return RESOURCE_TYPE_STREAMING;
   return RESOURCE_TYPE_CACHE;
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

bool Http::IsLastPacket(void *buffer, int size) {
   int result = memcmp(buffer + (size - ZERO_LENGTH), zero_, sizeof(zero_));
   return result == 0;
}
