#include "tecnicofs_client_api.h"

/* TODO: delete include */
#include <stdio.h>

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    /* TODO: Implement this / delete printf */
    printf("client path: %s\nserver pipe: %s\n", client_pipe_path, server_pipe_path);
    return -1;
}

int tfs_unmount() {
    /* TODO: Implement this */
    return -1;
}

int tfs_open(char const *name, int flags) {
    /* TODO: Implement this / delete printf */
    printf("name: %s\nflags: %d\n", name, flags);
    return -1;
}

int tfs_close(int fhandle) {
    /* TODO: Implement this / delete printf */
    printf("fhandle: %d\n", fhandle);
    return -1;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    /* TODO: Implement this / delete printf */
    printf("fhandle: %d\nbuffer: %p\nlen: %ld\n", fhandle, buffer, len);
    return -1;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    /* TODO: Implement this / delete printf */
    printf("fhandle: %d\nbuffer: %p\nlen: %ld\n", fhandle, buffer, len);
    return -1;
}

int tfs_shutdown_after_all_closed() {
    /* TODO: Implement this */
    return -1;
}
