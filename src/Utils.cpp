#include "Utils.hpp"

#include <algorithm>
#include <climits>
#include <cstring>

#include "Settings.hpp"

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

template <typename charT>
struct my_equal {
   my_equal(const std::locale &loc) : loc_(loc) {}
   bool operator()(charT ch1, charT ch2) {
      return std::toupper(ch1, loc_) == std::toupper(ch2, loc_);
   }

  private:
   const std::locale &loc_;
};

template <typename T>
int ci_find_substr(const T &str1, const T &str2,
                   const std::locale &loc = std::locale()) {
   typename T::const_iterator it =
       std::search(str1.begin(), str1.end(), str2.begin(), str2.end(),
                   my_equal<typename T::value_type>(loc));
   if (it != str1.end()) {
      return it - str1.begin();
   } else {
      return -1;  // not found
   }
}

std::string Utils::ReplaceHeaderTag(std::string header,
                                    const std::string &to_search,
                                    const std::string &replaced) {
   std::string tmp = "\r\n" + to_search + ":";
   int pos = ci_find_substr(header, tmp);
   if (pos < 0) {
      header = header + tmp + " " + replaced + "\r\n";
   } else {
      std::string first = header.substr(0, pos);
      std::string second = header.substr(pos + 2);
      header =
          first + tmp + " " + replaced + second.substr(second.find("\r\n"));
   }
   return header;
}

std::string Utils::GetHeaderTag(std::string header,
                                const std::string &to_search) {
   std::string tmp = "\r\n" + to_search + ":";
   int pos = ci_find_substr(header, tmp);
   if (pos < 0) {
      return std::string();
   } else {
      std::string first = header.substr(0, pos);
      std::string second = header.substr(pos + 4 + to_search.size());
      return second.substr(0, second.find("\r\n"));
   }
}
