#ifndef SRC_CACHE_HPP_
#define SRC_CACHE_HPP_

#include <liburing.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Request.hpp"
#include "Server.hpp"

class Cache {
 public:
  Cache();
  void SetRing(struct io_uring *ring);
  void SetServer(Server *server);

  void AddVerifyRequest();
  int HandleVerify();

  void AddExistsRequest(struct Request *request);
  int HandleExists(struct Request *request);

  void AddReadRequest(struct Request *request);
  int HandleRead(struct Request *request);

  void AddWriteRequest(struct Request *request);
  int HandleWrite(struct Request *request);

 private:
  struct File {
    void *data;
    size_t size;
  };

  struct io_uring *ring_;
  Server *server_;

  std::string GetUID(char *url);
  std::unordered_map<std::string, struct File> files_;
  std::unordered_map<std::string, std::vector<struct Request *>> waiting_read_;

  void AddCopyRequest(struct Request *request, File *file);
};
#endif
