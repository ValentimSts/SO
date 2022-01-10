#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>

void *test1(void *args) {

    char buffer[40];
    int f;
    ssize_t r;

    char *str = (char *)args;

    f = tfs_open("/f1", TFS_O_CREAT);
    assert(f != -1);

    r = tfs_write(f, str, strlen(str));
    assert(r == strlen(str));

    r = tfs_write(f, str, strlen(str));

    assert(tfs_close(f) != -1);

    f = tfs_open("/f1", 0);
    assert(f != -1);

    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    printf("bytes read: %ld\n", r);

    assert(tfs_close(f) != -1);

    buffer[r] = '\0';
    printf("%s\n", buffer);

    return NULL;
}

int main() {

    char *str1 = "AAA";
    char *str2 = "BB";
    char *str3 = "C";


    assert(tfs_init() != -1);

    test1(str1);
    test1(str2);
    test1(str3);

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");
}