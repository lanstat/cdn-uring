#ifndef SRC_REQUEST_HPP_
#define SRC_REQUEST_HPP_

struct Request {
   int event_type;
   int iovec_count;
   int client_socket;
   int cache_socket;
   uint64_t resource_id;
   struct iovec iov[];
};

#endif
