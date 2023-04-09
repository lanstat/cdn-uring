#ifndef SRC_REQUEST_HPP_
#define SRC_REQUEST_HPP_

struct Request {
   int event_type;
   int iovec_count;
   int client_socket;
   int auxiliar;
   uint64_t resource_id;
   unsigned int pivot;
   bool debug;
   bool is_processing;
   struct iovec iov[];
};

#endif
