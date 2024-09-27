#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Stack.h"

extern "C" {
uint32_t mtrack_stack(uint32_t maxFrames, const char *str, uint32_t strBytes);
}
Stack::Stack(unsigned skip) : mCount(0)
{
    const size_t bufferSize = 8 * 1024;
    char buffer[bufferSize];

    const uint32_t len = mtrack_stack(30, buffer, bufferSize);
    for(size_t i = 0, last = 0, line = 0; i < len; ++i) {
        if(buffer[i] == '\n') {
            if((!line && !strncmp(buffer+last, "Error", 5)) || (skip && skip--)) {
                buffer[i] = '\0';
                //printf("Skipped %zu/%u %s\n", line, skip, buffer+last);
            } else {
                buffer[i-1] = '\0';
                //printf("Found %zu %s\n", line, buffer+last);
                if(char *w = strstr(buffer+last, ":wasm-function[")) {
                    w[0] = '\0';
                    const char *url = w;
                    while(*(url-1) != '(')
                        --url;
                    const char *ip = w + 15;
                    while(*ip++ != ':');
                    //printf(" %u) (%s) (%s)\n", mCount, url, ip);
                    mPtrs[mCount] = strtol(ip, nullptr, 16);
                    mUrls[mCount] = url;
                    ++mCount;
                }
            }
            last = i + 1;
            ++line;
        }
    }
}
