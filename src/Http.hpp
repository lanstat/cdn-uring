#ifndef SRC_HTTP_HPP_
#define SRC_HTTP_HPP_

#include <liburing.h>

#include <iostream>
#include <string>
#include <unordered_map>

#include "Cache.hpp"
#include "Request.hpp"
#include "Server.hpp"
#include "Stream.hpp"

class Http {
 public:
  Http();
  virtual ~Http();

  void SetRing(struct io_uring *ring);
  void SetStream(Stream *stream);
  void SetCache(Cache *cache);

  void AddFetchDataRequest(struct Request *req);
  virtual int HandleReadHeaderRequest(struct Request *http, int readed) = 0;
  virtual bool HandleFetchRequest(struct Request *request, bool ipv4) = 0;
  virtual int HandleReadData(struct Request *request, int response) = 0;

 protected:
  int buffer_size_;
  struct io_uring *ring_;
  Cache *cache_;
  Stream *stream_;

  virtual void ReleaseSocket(struct Request *request) = 0;
  int GetResourceType(char *header, int size);

  int FetchHeaderLength(char *header, int size);
 private:
  void *zero_;
};
#endif
