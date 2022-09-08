#include <signal.h>

#include "src/Cache.hpp"
#include "src/Http.hpp"
#include "src/Dns.hpp"

void SigIntHandler(int signo) {
    printf("^C pressed. Shutting down.\n");
    exit(0);
}

int main() {
    signal(SIGINT, SigIntHandler);
    /*
    std::string url = "/1.vps.confiared.com/assets/img/logo.svg";

    auto http = new Http();
    http->Fetch(url);

    std::cout << std::endl;
    */
    auto dns = new Dns();
    dns->getAAAA("2.vps.confiared.com", false);
    //dns->parseEvent(1, dns->IPv4Socket);
    sleep(1);
    dns->parseEvent(1, dns->IPv6Socket);

    return 0;
}
