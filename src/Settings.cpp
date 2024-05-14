#include "Settings.hpp"

bool Settings::UseSSL = false;

int Settings::HttpBufferSize = 10240;

std::string Settings::CacheDir = "";

int Settings::ServerPort = 8000;

int Settings::DnsPort = 50053;

bool Settings::UseCache = true;

int Settings::CacheBufferSize = 64;

int Settings::StreamingBufferSize = 256;

bool Settings::HLSMode = false;

bool Settings::AstraMode = false;

int Settings::ListenMode = 1; // 1=IPv4 2=IPv6 3=unix

std::string Settings::Proxy = "";

std::string Settings::BaseUrl = "";

std::string Settings::HostFile = "";

std::string Settings::UnixPath = "/tmp/cdn.sock";
