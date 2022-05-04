#include "FileEmitter.h"

FileEmitter::~FileEmitter()
{
    fclose(mFile);
}

void FileEmitter::setFile(const std::string& file)
{
    mFile = fopen(file.c_str(), "w");
}

void FileEmitter::writeBytes(const void* data, size_t size, WriteType)
{
    if (mFile == nullptr) {
        fprintf(stderr, "file closed?\n");
        abort();
    }
    if (fwrite(data, size, 1, mFile) != 1) {
        fprintf(stderr, "file write error\n");
        abort();
    }
    mOffset += size;
}
