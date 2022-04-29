#pragma once

#include <common/Emitter.h>
#include <cstdio>
#include <string>

class FileEmitter : public Emitter
{
public:
    FileEmitter() = default;
    FileEmitter(const std::string& file)
    {
        setFile(file);
    }
    ~FileEmitter();

    void setFile(const std::string& file);

    virtual void writeBytes(const void* data, size_t size, WriteType type) override;

private:
    FILE* mFile { nullptr };
};
