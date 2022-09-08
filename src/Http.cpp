#include "Http.hpp"

Http::Http() {}

void Http::SetRing(struct io_uring *ring) { ring_ = ring; }

void Http::AddFetchDataRequest(struct Request *req) {}

int Http::HandleFetchData(struct Request *request) { return 0; }
