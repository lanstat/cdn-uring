#include "Utils.hpp"

#include <climits>

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

struct Request *Utils::CreateRequest(int iovec_count) {
   struct Request *req =
       (Request *)malloc(sizeof(*req) + sizeof(struct iovec) * iovec_count);
   req->iovec_count = iovec_count;
   for (int i = 0; i < req->iovec_count; i++) {
      req->iov[i].iov_len = 0;
   }
   return req;
}

void Utils::ReleaseRequest(struct Request *request) {
   for (int i = 0; i < request->iovec_count; i++) {
      if (request->iov[i].iov_len > 0) {
         free(request->iov[i].iov_base);
      }
   }
   free(request);
}

void Utils::OffsetMemcpy(char *dest, char *src, size_t offset, size_t size) {
   if (offset == 0) {
      for (size_t i = 0; i < size; ++i) {
         dest[i] = src[i];
      }
   } else if (size > 0) {
      char v0 = src[0];
      for (size_t i = 0; i < size; ++i) {
         char v1 = src[i + 1];
         dest[i] = (v0 << offset) | (v1 >> (CHAR_BIT - offset));
         v0 = v1;
      }
   }
}
