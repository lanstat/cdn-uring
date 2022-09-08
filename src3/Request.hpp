#ifndef SRC_REQUEST_HPP_
#define SRC_REQUEST_HPP_

struct Request {
   int event_type;
   int iovec_count;
   int client_socket;
   struct iovec iov[];
};

#endif
