#pragma once

#include <string>

class Parser
{
public:
    Parser() = default;

    bool parse(const std::string& line);

private:
    void handleModule(const char* data);
    void handleStack(const char* data);
};
