#ifndef SRC_REQUEST_HPP_
#define SRC_REQUEST_HPP_

#define REQUEST_STATUS_UNASSIGNED "0"
#define REQUEST_STATUS_IDDLE "1"
#define REQUEST_STATUS_PROCESSING "2"

struct Request {
   int event_type;
   int iovec_count;
   int client_socket;
   int index;
   int auxiliar;
   uint64_t resource_id;
   unsigned int pivot;
   bool debug;
   char status;
};

#endif
