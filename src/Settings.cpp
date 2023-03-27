#include "Settings.hpp"

bool Settings::UseSSL = false;

int Settings::HttpBufferSize = 102400;

std::string Settings::CacheDir = "";

int Settings::ServerPort = 8000;

bool Settings::UseCache = true;
