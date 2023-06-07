#ifndef SRC_SETTINGS_HPP_
#define SRC_SETTINGS_HPP_

#include <string>

class Settings {
  public:
   static bool UseSSL;
   static int HttpBufferSize;
   static int ServerPort;
   static int DnsPort;
   static std::string CacheDir;
   static bool UseCache;
   static int CacheBufferSize;
   static int StreamingBufferSize;
   static bool HLSMode;
   static int ReservedBufferSize;
};
#endif
