#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "src/Logger.hpp"

void SigIntHandler(int signo) {
    printf("^C pressed. Shutting down.\n");
    exit(0);
}

int main() {
    signal(SIGINT, SigIntHandler);
    Log(__FILE__, __LINE__) << "asdasdasdasd";

    return 0;
}
