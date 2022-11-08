#include "Logger.hpp"

#include <chrono>
#include <ctime>

bool Log::PrintDebug = false;
bool Log::NoLog = false;

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
   if (!CanPrint()) return;
   std::cout << std::endl;
}

bool Log::CanPrint() {
   if (NoLog) return false;
   if (mode_ == kDebug && !PrintDebug) return false;
   return true;
}

void Log::PrintHeader() {
   if (!CanPrint()) return;
   std::cout << currentDateTime();
   switch (mode_) {
      case kDebug:
         std::cout << " \033[1;34mDEBUG\033[0m   " << file_ << ":" << line_ << " ";
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
}

Log &Log::operator<<(int t) {
   if (!CanPrint()) return *this;
   std::cout << t;
   return *this;
}

Log &Log::operator<<(const char *t) {
   if (!CanPrint()) return *this;
   std::cout << t;
   return *this;
}

Log &Log::operator<<(const std::string &t) {
   if (!CanPrint()) return *this;
   std::cout << t;
   return *this;
}
