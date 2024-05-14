#include <liburing.h>
#include <signal.h>

#include <filesystem>

#include "src/Engine.hpp"
#include "src/Logger.hpp"
#include "src/Settings.hpp"

#define QUEUE_DEPTH 512

Engine* engine_ = nullptr;

void SigIntHandler(int signo) {
    printf("^C pressed. Shutting down.\n");
    io_uring_queue_exit(engine_->ring_);
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
        if (strcmp(argv[i], "-ssl") == 0) {
            Settings::UseSSL = true;
            continue;
        }
        if (memcmp(argv[i], "-buffer-size=", 13) == 0) {
            std::string tmp(argv[i]);
            Settings::HttpBufferSize = stoi(tmp.substr(13));
            continue;
        }
        if (memcmp(argv[i], "-server-port=", 13) == 0) {
            std::string tmp(argv[i]);
            Settings::ServerPort = stoi(tmp.substr(13));
            continue;
        }
        if (memcmp(argv[i], "-dns-port=", 10) == 0) {
            std::string tmp(argv[i]);
            Settings::DnsPort = stoi(tmp.substr(10));
            continue;
        }
        if (strcmp(argv[i], "-no-cache") == 0) {
            Settings::UseCache = false;
            continue;
        }
        if (strcmp(argv[i], "-hls") == 0) {
            Settings::HLSMode = true;
            continue;
        }
        if (strcmp(argv[i], "-astra") == 0) {
            Settings::AstraMode = true;
            continue;
        }
        if (memcmp(argv[i], "-cache-dir=", 11) == 0) {
            std::string tmp(argv[i]);
            Settings::CacheDir = tmp.substr(11);
            continue;
        }
        if (memcmp(argv[i], "-listen-mode=", 13) == 0) {
            std::string tmp(argv[i]);
            std::string mode = tmp.substr(13);
            if (mode == "unix") {
                Settings::ListenMode = 3;
            } else if (mode == "ipv6") {
                Settings::ListenMode = 2;
            } else {
                Settings::ListenMode = 1;
            }
            continue;
        }
        if (memcmp(argv[i], "-unix-path=", 11) == 0) {
            std::string tmp(argv[i]);
            Settings::UnixPath = tmp.substr(11);
            continue;
        }
        if (memcmp(argv[i], "-proxy=", 7) == 0) {
            std::string tmp(argv[i]);
            Settings::Proxy = tmp.substr(7);
            if (Settings::Proxy.front() != '/') {
                Settings::Proxy = "/" + Settings::Proxy;
            }
            continue;
        }
        if (memcmp(argv[i], "-base-url=", 10) == 0) {
            std::string tmp(argv[i]);
            Settings::BaseUrl = tmp.substr(10);
            if (Settings::BaseUrl.front() != '/') {
                Settings::BaseUrl = "/" + Settings::BaseUrl;
            }
            continue;
        }
        if (memcmp(argv[i], "-host-file=", 11) == 0) {
            std::string tmp(argv[i]);
            Settings::HostFile = tmp.substr(11);
            continue;
        }
    }
}

int main(int argc, char** argv) {
    ParserArguments(argc, argv);

    if (Settings::CacheDir.empty()) {
        std::filesystem::path cwd = std::filesystem::current_path() / "cache/";
        Settings::CacheDir = cwd.string();
    }

    if (!std::filesystem::is_directory(Settings::CacheDir) ||
        !std::filesystem::exists(Settings::CacheDir)) {
        std::filesystem::create_directory(Settings::CacheDir);
    }

    engine_ = new Engine();
    engine_->SetupListeningSocket(Settings::ServerPort);

    if (Settings::ListenMode == 3) {
        Log(__FILE__, __LINE__)
            << "Started server on " << Settings::UnixPath;
    } else {
        Log(__FILE__, __LINE__)
            << "Started server on localhost:" << Settings::ServerPort;
    }

    signal(SIGINT, SigIntHandler);
    signal(SIGPIPE, SIG_IGN);
    io_uring_queue_init(QUEUE_DEPTH, engine_->ring_, 0);
    engine_->Run();

    return 0;
}
