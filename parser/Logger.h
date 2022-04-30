#pragma once

#include <fmt/core.h>
#include <string>

class Logger
{
public:
    static Logger* instance() { return sInstance; }
    static void create(const std::string& file);

    void log(const std::string& msg);

private:
    Logger(const std::string& file);

private:
    static Logger* sInstance;

    FILE* mFile;
};

#define LOG(...)                                                \
    if (Logger::instance())                                     \
        Logger::instance()->log(fmt::format(__VA_ARGS__))
