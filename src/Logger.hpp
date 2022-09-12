#ifndef SRC_LOGGER_HPP_
#define SRC_LOGGER_HPP_

#include <iostream>
#include <string>

class Log {
  public:
   enum Mode {
      kDebug,
      kInfo,
      kError,
      kWarning
   };

   Log(const char *file, int line);
   Log(const char *file, int line, Mode mode);
   ~Log();

   Log &operator<<(int t);
   Log &operator<<(const char *t);
   Log &operator<<(const std::string &t);

  private:
   const char *file_;
   int line_;
   Mode mode_;

   void PrintHeader();
};
#endif
