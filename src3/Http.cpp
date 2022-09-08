#include "Http.hpp"

#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <cstring>
#include <fstream>
#include <sstream>

#define BUFFER_SIZE 1024
using namespace std;

std::vector<unsigned char> Http::Fetch(const std::string &url) {
    struct sockaddr_in client;
    std::vector<unsigned char> buffer;
    int port = 80;
    auto url_formated = ParseUrl(url);

    bzero(&client, sizeof(client));
    client.sin_family = AF_INET;
    client.sin_port = htons(port);

    if (inet_pton(AF_INET, "45.225.75.2", &client.sin_addr)
        <= 0) {
        printf(
            "\nInvalid address/ Address not supported \n");
        return buffer;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        cout << "Error creating socket." << endl;
        exit(1);
    }

    if (connect(sock, (struct sockaddr *)&client, sizeof(client)) < 0) {
        close(sock);
        cout << "Could not connect" << endl;
        exit(1);
    }

    stringstream ss;
    ss << "GET " << url_formated.at(1).c_str() <<" HTTP/1.1\r\n"
       << "Host: " << url_formated.at(0).c_str() << "\r\n"
       << "Accept: */*\r\n"
       << "Connection: close\r\n"
       << "\r\n\r\n";
    string request = ss.str();

    if (send(sock, request.c_str(), request.length(), 0) !=
        (int)request.length()) {
        cout << "Error sending request." << endl;
        exit(1);
    }

    unsigned char tmp[BUFFER_SIZE];
    int returned;
    while ((returned = read(sock, &tmp, BUFFER_SIZE)) > 0) {
        int pivot = returned / sizeof(tmp[0]);
        buffer.insert(buffer.end(), tmp, tmp + pivot);
    }
    close(sock);
    return buffer;
}

std::vector<std::string> Http::ParseUrl(const std::string &url) {
    std::string tmp = url.substr(1);
    std::cout<< "LAN_[" << __FILE__ << ":" << __LINE__ << "] "<< tmp << std::endl;
    std::vector<std::string> response;
    std::size_t found = tmp.find("/");
    response.push_back(tmp.substr(0, found));
    response.push_back(tmp.substr(found));
    return response;
}
