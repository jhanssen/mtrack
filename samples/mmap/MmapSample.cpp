#include <sys/mman.h>
#include <stdio.h>

#define PAGESIZE 4096
#define NUMPAGES 4

int main(int, char**)
{
    printf("mmapping\n");
    void* ptr = mmap(nullptr, PAGESIZE * NUMPAGES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!ptr) {
        printf("no mmap\n");
        return 0;
    }

    // touch the pages
    char* c = reinterpret_cast<char*>(ptr);
    for (int i = 0; i < NUMPAGES; ++i) {
        if (i % 2)
            continue;
        printf("touch page %d\n", i);
        *(c + (i * PAGESIZE) + 1) = 'a';
    }
    printf("sample done\n");
    munmap(ptr, PAGESIZE * NUMPAGES);
    return 0;
}
