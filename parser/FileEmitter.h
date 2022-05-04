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

    uint64_t offset() const { return mOffset; }

    void setFile(const std::string& file);

    virtual void writeBytes(const void* data, size_t size, WriteType type) override;

private:
    uint64_t mOffset {};
    FILE* mFile { nullptr };
};
