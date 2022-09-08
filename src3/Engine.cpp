#include "Engine.hpp"

#include "Cache.hpp"
#include "Http.hpp"

#define SERVER_STRING "Server: zerohttpd/0.1\r\n"
#define READ_SZ 8192

#define EVENT_TYPE_ACCEPT 0
#define EVENT_TYPE_READ 1
#define EVENT_TYPE_WRITE 2
#define EVENT_TYPE_FETCH 3
#define EVENT_TYPE_DNS_READ 4
#define EVENT_TYPE_DNS_DONE 5

const char *unimplemented_content =
    "HTTP/1.0 400 Bad Request\r\n"
    "Content-type: text/html\r\n"
    "\r\n"
    "<html>"
    "<head>"
    "<title>ZeroHTTPd: Unimplemented</title>"
    "</head>"
    "<body>"
    "<h1>Bad Request (Unimplemented)</h1>"
    "<p>Your client sent a request ZeroHTTPd did not understand and it is "
    "probably not your fault.</p>"
    "</body>"
    "</html>";

const char *http_404_content =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-type: text/html\r\n"
    "\r\n"
    "<html>"
    "<head>"
    "<title>ZeroHTTPd: Not Found</title>"
    "</head>"
    "<body>"
    "<h1>Not Found (404)</h1>"
    "<p>Your client is asking for an object that was not found on this "
    "server.</p>"
    "</body>"
    "</html>";

/*
 * Utility function to convert a string to lower case.
 * */

void Engine::strtolower(char *str) {
   for (; *str; ++str) *str = (char)tolower(*str);
}
/*
 One function that prints the system call and the error details
 and then exits with error code 1. Non-zero meaning things didn't go well.
 */
void Engine::FatalError(const char *syscall) {
   perror(syscall);
   exit(1);
}

/*
 * Helper function for cleaner looking code.
 * */

void *Engine::ZhMalloc(size_t size) {
   void *buf = malloc(size);
   if (!buf) {
      fprintf(stderr, "Fatal error: unable to allocate memory.\n");
      exit(1);
   }
   return buf;
}

Engine::Engine() { dns_ = new Dns(); }

Engine::~Engine() { delete dns_; }

/*
 * This function is responsible for setting up the main listening socket used by
 * the web server.
 * */

void Engine::SetupListeningSocket(int port) {
   int sock;
   struct sockaddr_in srv_addr;

   sock = socket(PF_INET, SOCK_STREAM, 0);
   if (sock == -1) {
      FatalError("socket()");
   }

   int enable = 1;
   if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
      FatalError("setsockopt(SO_REUSEADDR)");
   }

   memset(&srv_addr, 0, sizeof(srv_addr));
   srv_addr.sin_family = AF_INET;
   srv_addr.sin_port = htons(port);
   srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

   /* We bind to a port and turn this socket into a listening
    * socket.
    * */
   if (bind(sock, (const struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
      FatalError("bind()");
   }

   if (listen(sock, 10) < 0) {
      FatalError("listen()");
   }

   socket_ = sock;
}

int Engine::AddAcceptRequest(int server_socket, struct sockaddr_in *client_addr,
                             socklen_t *client_addr_len) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
   io_uring_prep_accept(sqe, server_socket, (struct sockaddr *)client_addr,
                        client_addr_len, 0);
   struct Request *req = (Request *)malloc(sizeof(*req));
   req->event_type = EVENT_TYPE_ACCEPT;
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(&ring);

   return 0;
}

int Engine::AddReadRequest(int client_socket) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
   struct Request *req = (Request *)malloc(sizeof(*req) + sizeof(struct iovec));
   req->iov[0].iov_base = malloc(READ_SZ);
   req->iov[0].iov_len = READ_SZ;
   req->event_type = EVENT_TYPE_READ;
   req->client_socket = client_socket;
   memset(req->iov[0].iov_base, 0, READ_SZ);
   /* Linux kernel 5.5 has support for readv, but not for recv() or read() */
   io_uring_prep_readv(sqe, client_socket, &req->iov[0], 1, 0);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(&ring);
   return 0;
}

int Engine::AddWriteRequest(struct Request *req) {
   struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
   req->event_type = EVENT_TYPE_WRITE;
   io_uring_prep_writev(sqe, req->client_socket, req->iov, req->iovec_count, 0);
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(&ring);
   return 0;
}

void Engine::SendStaticStringContent(const char *str, int client_socket) {
   struct Request *req =
       (Request *)ZhMalloc(sizeof(*req) + sizeof(struct iovec));
   unsigned long slen = strlen(str);
   req->iovec_count = 1;
   req->client_socket = client_socket;
   req->iov[0].iov_base = ZhMalloc(slen);
   req->iov[0].iov_len = slen;
   memcpy(req->iov[0].iov_base, str, slen);
   AddWriteRequest(req);
}

/*
 * When ZeroHTTPd encounters any other HTTP method other than GET or POST, this
 * function is used to inform the client.
 * */

void Engine::HandleUnimplementedMethod(int client_socket) {
   SendStaticStringContent(unimplemented_content, client_socket);
}

/*
 * This function is used to send a "HTTP Not Found" code and message to the
 * client in case the file requested is not found.
 * */

void Engine::HandleHttp404(int client_socket) {
   SendStaticStringContent(http_404_content, client_socket);
}

/*
 * Once a static file is identified to be served, this function is used to read
 * the file and write it over the client socket using Linux's sendfile() system
 * call. This saves us the hassle of transferring file buffers from kernel to
 * user space and back.
 * */

void Engine::CopyFileContents(char *file_path, off_t file_size,
                              struct iovec *iov) {
   int fd;

   char *buf = (char *)ZhMalloc(file_size);
   fd = open(file_path, O_RDONLY);
   if (fd < 0) {
      FatalError("open");
   }

   /* We should really check for short reads here */
   int ret = read(fd, buf, file_size);
   if (ret < file_size) {
      fprintf(stderr, "Encountered a short read.\n");
   }
   close(fd);

   iov->iov_base = buf;
   iov->iov_len = file_size;
}

/*
 * Simple function to get the file extension of the file that we are about to
 * serve.
 * */

const char *Engine::GetFilenameExt(const char *filename) {
   const char *dot = strrchr(filename, '.');
   if (!dot || dot == filename) return "";
   return dot + 1;
}

/*
 * Sends the HTTP 200 OK header, the server string, for a few types of files, it
 * can also send the content type based on the file extension. It also sends the
 * content length header. Finally it send a '\r\n' in a line by itself
 * signalling the end of headers and the beginning of any content.
 * */

void Engine::SendHeaders(const char *path, off_t len, struct iovec *iov) {
   // char small_case_path[1024];
   // char send_buffer[1024];
   // strcpy(small_case_path, path);
   // strtolower(small_case_path);

   // char *str = "HTTP/1.0 200 OK\r\n";

   // unsigned long slen = strlen(str);
   // iov[0].iov_base = ZhMalloc(slen);
   // iov[0].iov_len = slen;
   // memcpy(iov[0].iov_base, str, slen);

   // slen = strlen(SERVER_STRING);
   // iov[1].iov_base = ZhMalloc(slen);
   // iov[1].iov_len = slen;
   // memcpy(iov[1].iov_base, SERVER_STRING, slen);

   // const char *file_ext = GetFilenameExt(small_case_path);
   // if (strcmp("jpg", file_ext) == 0)
   // strcpy(send_buffer, "Content-Type: image/jpeg\r\n");
   // if (strcmp("jpeg", file_ext) == 0)
   // strcpy(send_buffer, "Content-Type: image/jpeg\r\n");
   // if (strcmp("png", file_ext) == 0)
   // strcpy(send_buffer, "Content-Type: image/png\r\n");
   // if (strcmp("gif", file_ext) == 0)
   // strcpy(send_buffer, "Content-Type: image/gif\r\n");
   // if (strcmp("htm", file_ext) == 0)
   // strcpy(send_buffer, "Content-Type: text/html\r\n");
   // if (strcmp("html", file_ext) == 0)
   // strcpy(send_buffer, "Content-Type: text/html\r\n");
   // if (strcmp("js", file_ext) == 0)
   // strcpy(send_buffer, "Content-Type: application/javascript\r\n");
   // if (strcmp("css", file_ext) == 0)
   // strcpy(send_buffer, "Content-Type: text/css\r\n");
   // if (strcmp("txt", file_ext) == 0)
   // strcpy(send_buffer, "Content-Type: text/plain\r\n");
   // slen = strlen(send_buffer);
   // iov[2].iov_base = ZhMalloc(slen);
   // iov[2].iov_len = slen;
   // memcpy(iov[2].iov_base, send_buffer, slen);

   //[> Send the content-length header, which is the file size in this case. <]
   // sprintf(send_buffer, "content-length: %ld\r\n", len);
   // slen = strlen(send_buffer);
   // iov[3].iov_base = ZhMalloc(slen);
   // iov[3].iov_len = slen;
   // memcpy(iov[3].iov_base, send_buffer, slen);

   /*
    * When the browser sees a '\r\n' sequence in a line on its own,
    * it understands there are no more headers. Content may follow.
    * */
   // strcpy(send_buffer, "\r\n");
   // slen = strlen(send_buffer);
   // iov[4].iov_base = ZhMalloc(slen);
   // iov[4].iov_len = slen;
   // memcpy(iov[4].iov_base, send_buffer, slen);
}

void Engine::HandleGetMethod(char *path, int client_socket) {
   auto cache = new Cache();
   auto uri = Cache::GetUID(path);
   std::cout << "LAN_[" << __FILE__ << ":" << __LINE__ << "] " << (char *)path
             << std::endl;

   struct stat path_stat;
   if (cache->IsValid(uri, &path_stat)) {
      if (S_ISREG(path_stat.st_mode)) {
         struct Request *req =
             (Request *)ZhMalloc(sizeof(*req) + (sizeof(struct iovec) * 6));
         req->iovec_count = 6;
         req->client_socket = client_socket;
         cache->Read(uri, path_stat.st_size, &req->iov[5]);
         std::cout << "LAN_[" << __FILE__ << ":" << __LINE__ << "] 202 " << uri
                   << " " << path_stat.st_size << " bytes" << std::endl;
         AddWriteRequest(req);
      } else {
         HandleHttp404(client_socket);
         printf("404 Not Found: %s\n", uri);
      }
   } else {
      AddDNSReadRequest(path, client_socket);
   }
   delete cache;
}

int Engine::AddFetchRequest(char *url, int client_socket) {
   struct Request *req =
       (Request *)ZhMalloc(sizeof(*req) + sizeof(struct iovec));
   unsigned long slen = strlen(url);
   req->iovec_count = 1;
   req->client_socket = client_socket;
   struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
   req->event_type = EVENT_TYPE_FETCH;
   req->iov[0].iov_base = ZhMalloc(slen);
   req->iov[0].iov_len = slen;
   memcpy(req->iov[0].iov_base, path, slen);

   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(&ring);

   return 0;
}

void Engine::HandleFetchData(struct Request *req) {
   int length = req->iov[0].iov_len;
   char path[length];
   memcpy(path, (char *)req->iov[0].iov_base, length);
   path[length] = '\0';
   int client_socket = req->client_socket;

   auto cache = new Cache();
   auto uri = Cache::GetUID(path);

   std::string url(path);
   auto http = new Http();
   auto buffer = http->Fetch(url);
   cache->WritePrev(uri, buffer);
   delete http;

   struct stat path_stat;
   if (!cache->IsValid(uri, &path_stat)) {
      printf("404 Not Found: %s (%s)\n", uri, path);
      HandleHttp404(client_socket);
   } else {
      struct Request *req =
          (Request *)ZhMalloc(sizeof(*req) + (sizeof(struct iovec) * 6));
      req->iovec_count = 6;
      req->client_socket = client_socket;
      cache->Read(uri, path_stat.st_size, &req->iov[5]);
      std::cout << "LAN_[" << __FILE__ << ":" << __LINE__ << "] 200 " << uri
                << " " << path_stat.st_size << " bytes" << std::endl;
      AddWriteRequest(req);
   }
   delete cache;
}

int Engine::AddDNSReadRequest(char *url, int client_socket) {
   struct Request *req =
       (Request *)ZhMalloc(sizeof(*req) + sizeof(struct iovec));
   unsigned long slen = strlen(url);
   req->iovec_count = 1;
   req->client_socket = client_socket;
   struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
   req->event_type = EVENT_TYPE_DNS_READ;
   req->iov[0].iov_base = ZhMalloc(slen);
   req->iov[0].iov_len = slen;
   memcpy(req->iov[0].iov_base, path, slen);

   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(&ring);

   return 0;
}

void Engine::HandleDNSReadRequest(struct Request *req) {}

/*
void Engine::HandleGetMethod(char *path, int client_socket) {
   char final_path[1024];

   *
    If a path ends in a trailing slash, the client probably wants the index
    file inside of that directory.
    *
   if (path[strlen(path) - 1] == '/') {
      strcpy(final_path, "public");
      strcat(final_path, path);
      strcat(final_path, "index.html");
   } else {
      strcpy(final_path, "public");
      strcat(final_path, path);
   }

   * The stat() system call will give you information about the file
    * like type (regular file, directory, etc), size, etc.
   struct stat path_stat;
   if (stat(final_path, &path_stat) == -1) {
      printf("404 Not Found: %s (%s)\n", final_path, path);
      HandleHttp404(client_socket);
   } else {
      * Check if this is a normal/regular file and not a directory or
       * something else
      if (S_ISREG(path_stat.st_mode)) {
         struct request *req =
             (request *)ZhMalloc(sizeof(*req) + (sizeof(struct iovec) * 6));
         req->iovec_count = 6;
         req->client_socket = client_socket;
         SendHeaders(final_path, path_stat.st_size, req->iov);
         CopyFileContents(final_path, path_stat.st_size, &req->iov[5]);
         std::cout << "LAN_[" << __FILE__ << ":" << __LINE__ << "] 200 "
                   << final_path << " " << path_stat.st_size << " bytes"
                   << std::endl;
         AddWriteRequest(req);
      } else {
         HandleHttp404(client_socket);
         printf("404 Not Found: %s\n", final_path);
      }
   }
}
*/

/*
 * This function looks at method used and calls the appropriate handler
 * function. Since we only implement GET and POST methods, it calls
 * handle_unimplemented_method() in case both these don't match. This sends an
 * error to the client.
 * */

void Engine::HandleHttpMethod(char *method_buffer, int client_socket) {
   char *method, *path, *saveptr;

   method = strtok_r(method_buffer, " ", &saveptr);
   strtolower(method);
   path = strtok_r(NULL, " ", &saveptr);

   if (strcmp(method, "get") == 0) {
      HandleGetMethod(path, client_socket);
   } else {
      HandleUnimplementedMethod(client_socket);
   }
}

int Engine::GetLine(const char *src, char *dest, int dest_sz) {
   for (int i = 0; i < dest_sz; i++) {
      dest[i] = src[i];
      if (src[i] == '\r' && src[i + 1] == '\n') {
         dest[i] = '\0';
         return 0;
      }
   }
   return 1;
}

int Engine::HandleClientRequest(struct Request *req) {
   char http_request[1024];
   /* Get the first line, which will be the Request */
   if (GetLine((char *)req->iov[0].iov_base, http_request,
               sizeof(http_request))) {
      fprintf(stderr, "Malformed request\n");
      exit(1);
   }
   HandleHttpMethod(http_request, req->client_socket);
   return 0;
}

void Engine::Run() {
   struct io_uring_cqe *cqe;
   struct sockaddr_in client_addr;
   socklen_t client_addr_len = sizeof(client_addr);

   AddAcceptRequest(socket_, &client_addr, &client_addr_len);

   while (1) {
      int ret = io_uring_wait_cqe(&ring, &cqe);
      if (ret < 0) {
         FatalError("io_uring_wait_cqe");
      }
      struct Request *req = (struct Request *)cqe->user_data;
      if (cqe->res < 0) {
         fprintf(stderr, "Async request failed: %s for event: %d\n",
                 strerror(-cqe->res), req->event_type);
         exit(1);
      }

      switch (req->event_type) {
         case EVENT_TYPE_ACCEPT:
            AddAcceptRequest(socket_, &client_addr, &client_addr_len);
            AddReadRequest(cqe->res);
            free(req);
            break;
         case EVENT_TYPE_READ:
            if (!cqe->res) {
               fprintf(stderr, "Empty request!\n");
               break;
            }
            HandleClientRequest(req);
            free(req->iov[0].iov_base);
            free(req);
            break;
         case EVENT_TYPE_DNS_READ:
            HandleDnsRead(req);
            free(req->iov[0].iov_base);
            free(req);
            break;
         case EVENT_TYPE_DNS_DONE:
            HandleFetchData(req);
            free(req->iov[0].iov_base);
            free(req);
            break;
         case EVENT_TYPE_FETCH:
            HandleFetchData(req);
            free(req->iov[0].iov_base);
            free(req);
            break;
         case EVENT_TYPE_WRITE:
            for (int i = 0; i < req->iovec_count; i++) {
               free(req->iov[i].iov_base);
            }
            close(req->client_socket);
            free(req);
            break;
      }

      /* Mark this request as processed */
      io_uring_cqe_seen(&ring, cqe);
   }
}
