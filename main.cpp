#include <liburing.h>
#include <signal.h>

#include "src/Engine.hpp"
#include "src/Logger.hpp"

#define DEFAULT_SERVER_PORT 8000
#define QUEUE_DEPTH 512

Engine* engine_ = nullptr;

void SigIntHandler(int signo) {
    printf("^C pressed. Shutting down.\n");
    io_uring_queue_exit(&engine_->ring_);
    exit(0);
}

void ParserArguments(int argc, char** argv) {
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-debug") == 0) {
            Log::PrintDebug = true;
            continue;
        }
        if (strcmp(argv[i], "-silent") == 0) {
            Log::NoLog = true;
            continue;
        }
    }
}

int main(int argc, char** argv) {
    ParserArguments(argc, argv);
    engine_ = new Engine();
    engine_->SetupListeningSocket(DEFAULT_SERVER_PORT);

    Log(__FILE__, __LINE__)
        << "Started server on localhost:" << DEFAULT_SERVER_PORT;

    signal(SIGINT, SigIntHandler);
    io_uring_queue_init(QUEUE_DEPTH, &engine_->ring_, 0);
    engine_->Run();

    return 0;
}
