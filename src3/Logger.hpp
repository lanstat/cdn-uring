#ifndef SRC_LOGGER_HPP_
#define SRC_LOGGER_HPP_

#include <iostream>
#include <string>
#include <vector>

class Logger {
   public:
    static void Write(const std::string &message);
};
#endif
