#ifndef SRC_ASTRAHTTPCLIENT_HPP_
#define SRC_ASTRAHTTPCLIENT_HPP_

#include <liburing.h>

#include <chrono>
#include <iostream>
#include <string>
#include <unordered_map>

#include "Cache.hpp"
#include "HttpClient.hpp"
#include "Request.hpp"
#include "Server.hpp"

class AstraHttpClient : public HttpClient {
 public:
  AstraHttpClient();
  int HandleReadHeaderRequest(struct Request *http, int readed) override;
  int HandleReadData(struct Request *request, int readed) override;

 private:
  struct Playlist {
    std::string url;
    std::string last_track;
    std::vector<std::string> tracks;
    std::vector<int> tracks_duration;
    std::chrono::time_point<std::chrono::system_clock> start_time;
  };
  std::unordered_map<uint64_t, struct Playlist *> playlist_;

  bool ProcessPlaylist(struct Request *http);
  bool VerifyContent(struct Request *http);
  bool ProcessReproduction(struct Request *http);
  void ReleaseSocket(struct Request *http) override;
  void RequestTrack(struct Request *http, std::string url, int msecs = 0);
  void RequestPlaylist(struct Request *http, std::string url);
  bool PlayNextTrack(struct Request *http);
  void ReleasePlaylist(struct Request *http);
  void CreatePlaylist(struct Request *http);
  int GetTimeout(struct Request *http);

  bool ExistsPlaylist(struct Request *http);
};
#endif
