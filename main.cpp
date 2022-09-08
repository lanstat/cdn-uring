#include <liburing.h>
#include <signal.h>

#include "src/Engine.hpp"

#define DEFAULT_SERVER_PORT 8000
#define QUEUE_DEPTH 256

Engine* engine_ = nullptr;

void SigIntHandler(int signo) {
   printf("^C pressed. Shutting down.\n");
   io_uring_queue_exit(&engine_->ring_);
   exit(0);
}

int main() {
    engine_ = new Engine();
    engine_->SetupListeningSocket(DEFAULT_SERVER_PORT);

    std::cout << "LAN_[" << __FILE__ << ":" << __LINE__ << "] "
              << "Started server on localhost:" << DEFAULT_SERVER_PORT
              << std::endl;

    signal(SIGINT, SigIntHandler);
    io_uring_queue_init(QUEUE_DEPTH, &engine_->ring_, 0);
    engine_->Run();

    return 0;
}
