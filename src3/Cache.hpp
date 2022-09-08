#ifndef SRC_CACHE_HPP_
#define SRC_CACHE_HPP_

#include <fcntl.h>
#include <sys/stat.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

class Cache {
  public:
   static char *GetUID(char *url);
   bool IsValid(char *uri, struct stat *path_stat);
   void Read(char *uri, off_t file_size, struct iovec *iov);
   void Write(const std::string &uid, char *raw);
   std::vector<unsigned char> ReadPrev(const std::string &uid);
   void WritePrev(const std::string &uri, std::vector<unsigned char> raw);
   void *ZhMalloc(size_t size);
};
#endif
