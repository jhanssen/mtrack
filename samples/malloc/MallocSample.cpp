#include <stdlib.h>
#include <unistd.h>

int main()
{
    // sleep(5);
    char *foo = static_cast<char *>(malloc(32));
    free(foo);
    foo = static_cast<char *>(malloc(32));
    free(foo);
    return 0;
}
