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

#include "Request.hpp"
#include "Dns.hpp"

class Engine {
  public:
   struct io_uring ring;

   Engine();
   ~Engine();
   void SetupListeningSocket(int port);
   void Run();

  private:
   int socket_;
   Dns *dns_;

   int HandleClientRequest(struct Request *req);
   int AddWriteRequest(struct Request *req);

   int AddFetchRequest(char *url, int client_socket);
   void HandleFetchData(struct Request *req);

   int AddDNSReadRequest(char *url, int client_socket);
   void HandleDNSReadRequest(struct Request *req);

   void SendHeaders(const char *path, off_t len, struct iovec *iov);
   void HandleGetMethod(char *path, int client_socket);
   int AddReadRequest(int client_socket);
   void HandleHttpMethod(char *method_buffer, int client_socket);
   void HandleUnimplementedMethod(int client_socket);
   void HandleHttp404(int client_socket);
   void CopyFileContents(char *file_path, off_t file_size, struct iovec *iov);
   int AddAcceptRequest(int server_socket, struct sockaddr_in *client_addr,
                        socklen_t *client_addr_len);
   void *ZhMalloc(size_t size);
   void FatalError(const char *syscall);
   int GetLine(const char *src, char *dest, int dest_sz);
   void SendStaticStringContent(const char *str, int client_socket);
   const char *GetFilenameExt(const char *filename);
   void strtolower(char *str);
};
#endif
