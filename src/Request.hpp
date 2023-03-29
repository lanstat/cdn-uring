#ifndef SRC_REQUEST_HPP_
#define SRC_REQUEST_HPP_

struct Request {
   int event_type;
   int iovec_count;
   int client_socket;
   uint64_t resource_id;
   int packet_count;
   bool debug;
   bool is_processing;
   struct iovec iov[];
};

#endif
