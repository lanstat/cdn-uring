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
   static std::string ReplaceHeaderTag(std::string header,
                                       const std::string &to_search,
                                       const std::string &replaced);
   static std::string GetHeaderTag(std::string header,
                                   const std::string &to_search);
};
#endif
