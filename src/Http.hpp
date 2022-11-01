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

   void SetRing(struct io_uring *ring);
   void SetServer(Server *server);
   void SetCache(Cache *cache);

   void AddFetchDataRequest(struct Request *req);
   virtual int HandleFetchData(struct Request *request) = 0;
   void AddReadRequest(struct Request *request, int fd);
   virtual int HandleReadData(struct Request *request) = 0;

  protected:
   struct HttpRequest {
      struct Request *request;
      std::vector<struct iovec> buffer;
      int size;
   };

   int buffer_size_;
   struct io_uring *ring_;
   Server *server_;
   Cache *cache_;
   std::unordered_map<int, struct HttpRequest *> waiting_read_;

   int GetDataReadedLength(char *src);
   void ReleaseSocket(struct Request *request);
   struct Request *UnifyBuffer(struct Request *request);
};
#endif
