#ifndef SRC_MUX_HPP_
#define SRC_MUX_HPP_

#include <liburing.h>

#include <unordered_map>
#include <vector>
#include <string>

#include "Request.hpp"

struct File {
   void *data;
   size_t size;
};

struct Mux {
   std::vector<struct Request *> requests;
   struct iovec header;
   unsigned int count;
   std::vector<struct iovec> buffer;
   int type;
   std::string path;
};
#endif
