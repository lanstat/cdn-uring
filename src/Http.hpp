#ifndef SRC_HTTP_HPP_
#define SRC_HTTP_HPP_

#include <liburing.h>

#include <iostream>
#include <string>
#include <unordered_map>

#include "Cache.hpp"
#include "Request.hpp"
#include "Server.hpp"

class Http {
 public:
  Http();
  virtual ~Http();

  void SetRing(struct io_uring *ring);
  void SetServer(Server *server);
  void SetCache(Cache *cache);

  void AddFetchDataRequest(struct Request *req);
  virtual int HandleFetchData(struct Request *request) = 0;
  virtual int HandleReadData(struct Request *request) = 0;

 protected:
  struct HttpRequest {
    struct Request *request;
    std::vector<struct iovec> buffer;
    int size;
    int header_size;
  };

  int buffer_size_;
  struct io_uring *ring_;
  Cache *cache_;
  std::unordered_map<int, struct HttpRequest *> waiting_read_;

  int GetDataReadedLength(void *src, struct HttpRequest *request);
  virtual void ReleaseSocket(struct Request *request) = 0;
  struct Request *UnifyBuffer(struct Request *request);

 private:
  void *zero_;

  std::string CreateHeader(char *prev_header);
};
#endif
