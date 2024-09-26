#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern "C" {
void mtrack_snapshot(const char *name);
}

void doThing()
{
    char *foo = static_cast<char *>(malloc(33*1024));
    free(foo);
    foo = static_cast<char *>(malloc(66*1024));
    mtrack_snapshot("go");
    printf("sleep\n");
    //sleep(2000);
    printf("awake\n");
    free(foo);
}

int main()
{
    doThing();
    return 0;
}
