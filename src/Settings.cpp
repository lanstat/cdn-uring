#include "Settings.hpp"

bool Settings::UseSSL = false;

int Settings::HttpBufferSize = 2048;

std::string Settings::CacheDir = "";

int Settings::ServerPort = 8000;

int Settings::DnsPort = 50053;

bool Settings::UseCache = true;

int Settings::CacheBufferSize = 64;

int Settings::StreamingBufferSize = 256;

bool Settings::HLSMode = false;

int Settings::ReservedBufferSize = 51200;
