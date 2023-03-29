#ifndef SRC_MUX_HPP_
#define SRC_MUX_HPP_

#include <liburing.h>

#include <unordered_map>

#include "Request.hpp"

struct File {
   void *data;
   size_t size;
};

struct Mux {
   std::vector<struct Request *> requests;
   std::vector<struct iovec> header;
   int count;
   std::unordered_map<int, struct iovec> buffer;
};
#endif
