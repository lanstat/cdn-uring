#include "Engine.hpp"

#include "EventType.hpp"
#include "HttpClient.hpp"
#include "HttpsClient.hpp"
#include "Logger.hpp"
#include "Settings.hpp"
#include "Utils.hpp"

void PrintRequestType(int type) {
   std::string type_str;
   switch (type) {
      case EVENT_TYPE_ACCEPT:
         type_str = "EVENT_TYPE_ACCEPT";
         break;
      case EVENT_TYPE_SERVER_READ:
         type_str = "EVENT_TYPE_SERVER_READ";
         break;
      case EVENT_TYPE_SERVER_WRITE_COMPLETE:
         type_str = "EVENT_TYPE_SERVER_WRITE_COMPLETE";
         break;
      case EVENT_TYPE_SERVER_WRITE_PARTIAL:
         type_str = "EVENT_TYPE_SERVER_WRITE_PARTIAL";
         break;
      case EVENT_TYPE_SERVER_CLOSE:
         type_str = "EVENT_TYPE_SERVER_CLOSE";
         break;
      case EVENT_TYPE_HTTP_FETCH:
         type_str = "EVENT_TYPE_HTTP_FETCH";
         break;
      case EVENT_TYPE_HTTP_FETCH_IPV4:
         type_str = "EVENT_TYPE_HTTP_FETCH_IPV4";
         break;
      case EVENT_TYPE_HTTP_READ_HEADER:
         type_str = "EVENT_TYPE_HTTP_READ_HEADER";
         break;
      case EVENT_TYPE_HTTP_READ_CONTENT:
         type_str = "EVENT_TYPE_HTTP_READ_CONTENT";
         break;
      case EVENT_TYPE_DNS_VERIFY:
         type_str = "EVENT_TYPE_DNS_VERIFY";
         break;
      case EVENT_TYPE_DNS_FETCHAAAA:
         type_str = "EVENT_TYPE_DNS_FETCHAAAA";
         break;
      case EVENT_TYPE_CACHE_EXISTS:
         type_str = "EVENT_TYPE_CACHE_EXISTS";
         break;
      case EVENT_TYPE_CACHE_READ_HEADER:
         type_str = "EVENT_TYPE_CACHE_READ_HEADER";
         break;
      case EVENT_TYPE_CACHE_READ_CONTENT:
         type_str = "EVENT_TYPE_CACHE_READ_CONTENT";
         break;
      case EVENT_TYPE_CACHE_VERIFY_INVALID:
         type_str = "EVENT_TYPE_CACHE_VERIFY_INVALID";
         break;
      case EVENT_TYPE_CACHE_WRITE_HEADER:
         type_str = "EVENT_TYPE_CACHE_WRITE_HEADER";
         break;
      case EVENT_TYPE_CACHE_WRITE_CONTENT:
         type_str = "EVENT_TYPE_DNS_FETCHAAAA";
         break;
      case EVENT_TYPE_CACHE_CLOSE:
         type_str = "EVENT_TYPE_CACHE_CLOSE";
         break;
   }
   Log(__FILE__, __LINE__, Log::kDebug) << type_str;
}

Engine::Engine() {
   dns_ = new Dns();
   server_ = new Server();
   cache_ = new Cache();
   stream_ = new Stream();

   if (Settings::UseSSL) {
      http_ = new HttpsClient();
   } else {
      http_ = new HttpClient();
   }

   ring_ = (struct io_uring *)malloc(sizeof(struct io_uring));
}

Engine::~Engine() {
   delete dns_;
   delete server_;
   delete cache_;
   delete stream_;
   delete http_;

   free(ring_);
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
 * This function is responsible for setting up the main listening socket used by
 * the web server.
 * */
void Engine::SetupListeningSocket(int port) {
   if (Settings::IPv6Mode) {
      ListenIpv6(port);
   } else {
      ListenIpv4(port);
   }
}

void Engine::ListenIpv4(int port) {
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

void Engine::ListenIpv6(int port) {
   int sock;
   struct sockaddr_in6 srv_addr;

   sock = socket(PF_INET6, SOCK_STREAM, 0);
   if (sock == -1) {
      FatalError("socket()");
   }

   int enable = 1;
   if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
      FatalError("setsockopt(SO_REUSEADDR)");
   }

   memset(&srv_addr, 0, sizeof(srv_addr));
   srv_addr.sin6_family = AF_INET6;
   srv_addr.sin6_port = htons(port);
   srv_addr.sin6_addr = in6addr_any;

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
   struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
   io_uring_prep_accept(sqe, server_socket, (struct sockaddr *)client_addr,
                        client_addr_len, 0);
   struct Request *req = (Request *)malloc(sizeof(*req));
   req->event_type = EVENT_TYPE_ACCEPT;
   req->iovec_count = 0;
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(ring_);

   return 0;
}

void Engine::Run() {
   struct io_uring_cqe *cqe;
   struct sockaddr_in client_addr;
   socklen_t client_addr_len = sizeof(client_addr);

   server_->SetRing(ring_);

   stream_->SetServer(server_);
   stream_->SetRing(ring_);

   cache_->SetRing(ring_);
   cache_->SetStream(stream_);

   dns_->SetRing(ring_);

   http_->SetRing(ring_);
   http_->SetCache(cache_);
   http_->SetStream(stream_);

   AddAcceptRequest(socket_, &client_addr, &client_addr_len);
   dns_->AddVerifyUDPRequest();
   // cache_->AddVerifyRequest();

   while (1) {
      int ret = io_uring_wait_cqe(ring_, &cqe);
      if (ret < 0) {
         FatalError("io_uring_wait_cqe");
      }
      struct Request *request = (struct Request *)cqe->user_data;
      int response = (int)cqe->res;

      request->is_processing = false;

      if (request->event_type != EVENT_TYPE_DNS_VERIFY) {
         PrintRequestType(request->event_type);
      }

      switch (request->event_type) {
         case EVENT_TYPE_ACCEPT:
            Utils::ReleaseRequest(request);
            Log(__FILE__, __LINE__) << "Accept http request";

            AddAcceptRequest(socket_, &client_addr, &client_addr_len);
            server_->AddReadRequest(cqe->res);
            break;
         case EVENT_TYPE_SERVER_READ: {
            if (server_->HandleRead(request)) {
               if (!stream_->HandleExistsResource(request)) {
                  cache_->AddExistsRequest(request);
               }
            }
            Utils::ReleaseRequest(request);
         } break;
         case EVENT_TYPE_SERVER_WRITE_COMPLETE:
            server_->HandleWrite(request, response);
         case EVENT_TYPE_SERVER_WRITE_PARTIAL:
            // If failed to write to socket
            if (server_->HandleWriteStream(request, response) == 1) {
               stream_->RemoveRequest(request);
               Utils::ReleaseRequest(request);
               Log(__FILE__, __LINE__) << "Remove client closed";
            }
            break;
         case EVENT_TYPE_SERVER_CLOSE:
            server_->HandleClose(request);
            break;
         case EVENT_TYPE_SERVER_WRITE_HEADER:
            stream_->HandleWriteHeaders(request);
            break;
         case EVENT_TYPE_CACHE_EXISTS:
            if (!cache_->HandleExists(request)) {
               dns_->AddFetchAAAARequest(request, Settings::UseSSL);
            } else {
               cache_->AddReadHeaderRequest(request);
               Utils::ReleaseRequest(request);
            }
            break;
         case EVENT_TYPE_CACHE_READ_HEADER:
            cache_->HandleReadHeader(request, response);
            break;
         case EVENT_TYPE_CACHE_READ_CONTENT:
            if (!stream_->HandleReadCacheRequest(request, response)) {
               stream_->RemoveRequest(request);
            }
            break;
         case EVENT_TYPE_CACHE_COPY_CONTENT:
            stream_->HandleCopyCacheRequest(request, response);
            break;
         case EVENT_TYPE_CACHE_WRITE_HEADER:
            cache_->HandleWriteHeader(request, response);
            break;
         case EVENT_TYPE_CACHE_WRITE_CONTENT:
            cache_->HandleWrite(request);
            break;
         case EVENT_TYPE_CACHE_CLOSE:
            break;
         case EVENT_TYPE_DNS_VERIFY:
            dns_->HandleVerifyUDP();
            dns_->AddVerifyUDPRequest();
            Utils::ReleaseRequest(request);
            break;
         case EVENT_TYPE_HTTP_FETCH:
            http_->HandleFetchRequest(request, false);
            Utils::ReleaseRequest(request);
            break;
         case EVENT_TYPE_HTTP_FETCH_IPV4:
            http_->HandleFetchRequest(request, true);
            Utils::ReleaseRequest(request);
            break;
         case EVENT_TYPE_HTTP_READ_HEADER:
            http_->HandleReadHeaderRequest(request, response);
            break;
         case EVENT_TYPE_HTTP_READ_CONTENT:
            http_->HandleReadData(request, response);
            break;
         case EVENT_TYPE_CACHE_VERIFY_INVALID:
            // TODO(lanstat): add verify cache
            break;
      }

      /* Mark this request as processed */
      io_uring_cqe_seen(ring_, cqe);
   }
}
