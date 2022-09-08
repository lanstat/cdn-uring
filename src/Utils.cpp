#include "Utils.hpp"

/*
 * Utility function to convert a string to lower case.
 * */
void Utils::StrToLower(char *str) {
   for (; *str; ++str) *str = (char)tolower(*str);
}

/*
 * Helper function for cleaner looking code.
 * */
void *Utils::ZhMalloc(size_t size) {
   void *buf = malloc(size);
   if (!buf) {
      fprintf(stderr, "Fatal error: unable to allocate memory.\n");
      exit(1);
   }
   return buf;
}

struct Request *Utils::CreateRequest() {
   struct Request *req = (Request *)malloc(sizeof(*req) + sizeof(struct iovec));
   return req;
}
