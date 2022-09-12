#include "Http.hpp"

#include <arpa/inet.h>
#include <string.h>

#include <sstream>
#include <vector>

#include "Logger.hpp"
#define BUFFER_SIZE 1024

Http::Http() {}

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::AddFetchDataRequest(struct Request *req) {}

int Http::HandleFetchData(struct Request *request) {
   Fetch(request);
   return 0;
}

void Http::Fetch(struct Request *request) {
   std::vector<unsigned char> buffer;

   std::string url((char *)request->iov[0].iov_base);
   url = url.substr(1);
   std::size_t pos = url.find("/");
   std::string host = url.substr(0, pos);
   std::string query = url.substr(pos);

   Log(__FILE__, __LINE__) << query;

   struct sockaddr_in6 *client =
       (struct sockaddr_in6 *)request->iov[4].iov_base;
   int sock = socket(AF_INET6, SOCK_STREAM, 0);

   if (sock < 0) {
      Log(__FILE__, __LINE__, Log::kError) << "Error creating socket";
      exit(1);
   }

   if (connect(sock, (struct sockaddr *)client, sizeof(struct sockaddr_in6)) <
       0) {
      close(sock);
      Log(__FILE__, __LINE__, Log::kError) << "Could not connect ";
      exit(1);
   }

   std::stringstream ss;
   ss << "GET " << query << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << "Accept: */*\r\n"
      << "Connection: close\r\n"
      << "\r\n\r\n";
   std::string request_data = ss.str();

   if (send(sock, request_data.c_str(), request_data.length(), 0) !=
       (int)request_data.length()) {
      Log(__FILE__, __LINE__, Log::kError) << "invalid socket";
      exit(1);
   }

   unsigned char tmp[BUFFER_SIZE];
   int returned;
   while ((returned = read(sock, &tmp, BUFFER_SIZE)) > 0) {
      int pivot = returned / sizeof(tmp[0]);
      buffer.insert(buffer.end(), tmp, tmp + pivot);
   }
   close(sock);

   request->iov[3].iov_base = malloc(buffer.size());
   request->iov[3].iov_len = buffer.size();

   memcpy(request->iov[3].iov_base, buffer.data(), buffer.size());
}
