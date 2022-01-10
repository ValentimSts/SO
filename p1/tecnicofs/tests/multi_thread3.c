#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <pthread.h>

int main() {

    /*
    char *str = "AAA!";
    char *path = "/f1";
    char buffer[40];
    pthread_t threads[3];
    */

    assert(tfs_init() != -1);



    assert(tfs_destroy() != -1);
}