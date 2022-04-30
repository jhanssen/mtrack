#include "Logger.h"
#include <cstdio>

Logger* Logger::sInstance = nullptr;

Logger::Logger(const std::string& file)
{
    mFile = ::fopen(file.c_str(), "w");
    if (!mFile)
        abort();
}

void Logger::create(const std::string& file)
{
    sInstance = new Logger(file);
}

void Logger::log(const std::string& msg)
{
    if (msg.empty())
        return;
    ::fwrite(msg.c_str(), msg.size(), 1, mFile);
    if (msg.back() != '\n') {
        ::fwrite("\n", 1, 1, mFile);
    }
    ::fflush(mFile);
}
