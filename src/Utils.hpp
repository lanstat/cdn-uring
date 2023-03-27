#ifndef SRC_UTILS_HPP_
#define SRC_UTILS_HPP_

#include <liburing.h>

#include <iostream>
#include <string>

#include "Request.hpp"

class Utils {
  public:
   static void *ZhMalloc(size_t size);
   static void StrToLower(char *str);
   static struct Request *CreateRequest(int iovec_count);
   static struct Request *HttpEntryRequest();
   static struct Request *HttpErrorRequest();
   static struct Request *HttpInnerRequest();
   static struct Request *HttpExternalRequest();
   static struct Request *HttpsExternalRequest();
   static void ReleaseRequest(struct Request *request);
};
#endif
