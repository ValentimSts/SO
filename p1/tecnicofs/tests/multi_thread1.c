#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>

void *test1(void *args) {

    char buffer[40];
    int f;
    ssize_t r;

    char *file = (char *)args;

    f = tfs_open(file, TFS_O_CREAT);
    assert(f != -1);

    r = tfs_read(f, buffer, sizeof(buffer) - 1);

    assert(tfs_close(f) != -1);

    buffer[r] = '\0';
    printf("number of bytes read: %ld\n", r);
    printf("buffer: %s\n", buffer);

    return NULL;
}

/* reads the same file with multiple threads at the same time */

int main() {

    int f;
    ssize_t r;
    char file[4] = "/f1";
    char *str = "JIQIJIQIJIQIJWHOSAYSIMGUEIQIJIQIQJIQJQIQJIQ ";

    pthread_t threads[3];

    assert(tfs_init() != -1);

    /* Creates a file */
    f = tfs_open(file, TFS_O_CREAT);
    assert(f != -1);

    r = tfs_write(f, str, strlen(str));
    assert(r == strlen(str));

    assert(tfs_close(f) != -1);

    assert(pthread_create(&threads[0], NULL, test1, file) == 0);
    assert(pthread_create(&threads[1], NULL, test1, file) == 0);
    assert(pthread_create(&threads[2], NULL, test1, file) == 0);

    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    assert(pthread_join(threads[2], NULL) == 0);

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");
}