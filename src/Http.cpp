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

const char *end_line = "\r\n";
const char *ws = " \t\n\r\f\v";

Http::Http() {
   stream_ = nullptr;
   cache_ = nullptr;

   buffer_size_ = Settings::HttpBufferSize;
}

Http::~Http() {}

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::SetCache(Cache *cache) { cache_ = cache; }

void Http::SetStream(Stream *stream) { stream_ = stream; }

void Http::AddFetchDataRequest(struct Request *req) {}

int Http::GetResourceType(char *header, int size) {
   return RESOURCE_TYPE_STREAMING;
   //return RESOURCE_TYPE_CACHE;
}
