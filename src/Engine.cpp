#include "Engine.hpp"

#include "EventType.hpp"
#include "Utils.hpp"

#define READ_SZ 8192

Engine::Engine() {
   dns_ = new Dns();
   server_ = new Server();
   cache_ = new Cache();
   http_ = new Http();
}

Engine::~Engine() {
   delete dns_;
   delete server_;
   delete cache_;
   delete http_;
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
   struct io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
   io_uring_prep_accept(sqe, server_socket, (struct sockaddr *)client_addr,
                        client_addr_len, 0);
   struct Request *req = (Request *)malloc(sizeof(*req));
   req->event_type = EVENT_TYPE_ACCEPT;
   req->iovec_count = 0;
   io_uring_sqe_set_data(sqe, req);
   io_uring_submit(&ring_);

   return 0;
}

void Engine::Run() {
   struct io_uring_cqe *cqe;
   struct sockaddr_in client_addr;
   socklen_t client_addr_len = sizeof(client_addr);
   server_->SetRing(&ring_);
   dns_->SetRing(&ring_);
   cache_->SetRing(&ring_);
   http_->SetRing(&ring_);

   AddAcceptRequest(socket_, &client_addr, &client_addr_len);
   // dns_->AddVerifyUDPRequest();
   // cache_->AddVerifyRequest();

   while (1) {
      int ret = io_uring_wait_cqe(&ring_, &cqe);
      if (ret < 0) {
         FatalError("io_uring_wait_cqe");
      }
      struct Request *request = (struct Request *)cqe->user_data;

      std::cout << "LAN_[" << __FILE__ << ":" << __LINE__ << "] "
                << request->event_type << std::endl;

      switch (request->event_type) {
         case EVENT_TYPE_ACCEPT:
            Utils::ReleaseRequest(request);

            AddAcceptRequest(socket_, &client_addr, &client_addr_len);
            server_->AddReadRequest(cqe->res);
            break;
         case EVENT_TYPE_SERVER_READ: {
            auto inner_request = Utils::CreateRequest(4);

            bool is_method_valid = server_->HandleRead(request, inner_request);
            if (is_method_valid) {
               cache_->AddExistsRequest(inner_request);
            } else {
               server_->AddHttpErrorRequest(inner_request, 405);
            }
         } break;
         case EVENT_TYPE_SERVER_WRITE:
            server_->HandleWrite(request);
            std::cout << "LAN_[" << __FILE__ << ":" << __LINE__ << "] "
                      << "close socket" << std::endl;
            break;
         case EVENT_TYPE_CACHE_EXISTS:
            if (cache_->HandleExists(request) == 0) {
               cache_->AddReadRequest(request);
            } else {
               dns_->AddFetchAAAARequest(request);
            }
            break;
         case EVENT_TYPE_CACHE_READ:
            cache_->HandleRead(request);
            server_->AddWriteRequest(request);
            break;
            /*
            case EVENT_TYPE_CACHE_VERIFY:
               cache_->HandleVerify();
               cache_->AddVerifyRequest();
               break;
            case EVENT_TYPE_HTTP_FETCH:
               http_->HandleFetchData(req);
               cache_->AddReadRequest(req);
               break;
            case EVENT_TYPE_DNS_VERIFY:
               dns_->HandleVerifyUDP();
               dns_->AddVerifyUDPRequest();
               break;
            case EVENT_TYPE_DNS_FETCHAAAA:
               int dns_fetch = dns_->HandleFetchAAAA(req);
               if (dns_fetch == 0) {
                  http_->AddFetchDataRequest(req);
               }
               break;
               */
      }

      /* Mark this request as processed */
      io_uring_cqe_seen(&ring_, cqe);
   }
}
