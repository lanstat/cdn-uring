#include "Logger.hpp"

#include <chrono>
#include <ctime>

const std::string currentDateTime() {
   time_t now = time(0);
   struct tm tstruct;
   char buf[80];
   tstruct = *localtime(&now);
   strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);

   return buf;
}

Log::Log(const char *file, int line) {
   file_ = file;
   line_ = line;
   mode_ = kInfo;
   PrintHeader();
}

Log::Log(const char *file, int line, Mode mode) {
   file_ = file;
   line_ = line;
   mode_ = mode;
   PrintHeader();
}

Log::~Log() {
   std::cout << std::endl;
}

void Log::PrintHeader() {
   std::cout << currentDateTime();
   switch(mode_) {
      case kDebug:
         std::cout << " \033[1;34mDEBUG\033[0m   ";
         break;
      case kInfo:
         std::cout << " \033[1;36mINFO\033[0m    ";
         break;
      case kError:
         std::cout << " \033[1;31mERROR\033[0m   ";
         break;
      case kWarning:
         std::cout << " \033[1;33mWARNING\033[0m ";
         break;
   }
   std::cout << file_ << ":" << line_ << " ";
}

Log &Log::operator<<(int t) {
   std::cout << t;
   return *this;
}

Log &Log::operator<<(const char *t) {
   std::cout << t;
   return *this;
}

Log &Log::operator<<(const std::string &t) {
   std::cout << t;
   return *this;
}
