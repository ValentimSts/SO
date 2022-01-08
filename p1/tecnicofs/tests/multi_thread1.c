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

    assert(tfs_close(f) != -1);

    f = tfs_open("/f1", 0);
    assert(f != -1);

    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    assert(r == strlen(str));

    assert(tfs_close(f) != -1);

    buffer[r] = '\0';
    printf("%s\n", buffer);

    return NULL;
}

int main() {

    char *str1 = "AAA!";
    char *str2 = "BB!";
    char *str3 = "C!";

    pthread_t threads[3];

    assert(tfs_init() != -1);

    assert(pthread_create(&threads[0], NULL, test1, str1) == 0);
    assert(pthread_create(&threads[1], NULL, test1, str2) == 0);
    assert(pthread_create(&threads[2], NULL, test1, str3) == 0);

    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    assert(pthread_join(threads[2], NULL) == 0);

    assert(tfs_destroy() != -1);

    printf("Success!\n");
}