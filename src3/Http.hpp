#ifndef SRC_HTTP_HPP_
#define SRC_HTTP_HPP_

#include <iostream>
#include <string>
#include <vector>

class Http {
  public:
   std::vector<unsigned char> Fetch(const std::string &url);
  private:
   std::vector<std::string> ParseUrl(const std::string &url);
};
#endif
