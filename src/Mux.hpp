#ifndef SRC_MUX_HPP_
#define SRC_MUX_HPP_

#include <liburing.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "Request.hpp"

struct Node {
   unsigned int pivot;
   size_t size;
   struct Request *cache;
   struct iovec *buffer;
};

struct Mux {
   std::vector<struct Request *> requests;
   struct iovec header;
   unsigned int pivot;
   int type;
   int tag;
   bool is_completed;
   bool in_memory;
   std::string path;
   std::string url;
   std::string etag;
   struct iovec *buffer;
};
#endif
