#ifndef SRC_ENGINE_HPP_
#define SRC_ENGINE_HPP_

#include <ctype.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "Cache.hpp"
#include "Dns.hpp"
#include "Http.hpp"
#include "Request.hpp"
#include "Server.hpp"
#include "Stream.hpp"

class Engine {
  public:
   Engine();
   ~Engine();
   void SetupListeningSocket(int port);
   void Run();
   struct io_uring *ring_;

  private:
   int socket_;

   Dns *dns_;
   Cache *cache_;
   Http *http_;
   Server *server_;
   Stream *stream_;

   int AddAcceptRequest(int server_socket, struct sockaddr_in *client_addr,
                        socklen_t *client_addr_len);
   void FatalError(const char *syscall);

   void ListenIpv4(int port);
   void ListenIpv6(int port);
   void ListenUnixSocket();
};
#endif
