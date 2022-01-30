#include "tecnicofs_client_api.h"


/* TODO: check if we can include the stuffs and whether they
         can stay here or be declared in common.h */

/* Extra includes */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>


/* Global variables used to store both the server and clients's file descriptors */
int client_fd;
int server_fd;

/* Stores the current client session id. Returned by the server after
 * each successfull command */
int curr_session_id;


int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    /* TODO: do we need to check if the client name is too big? */
    int c_path_size = strlen(client_pipe_path);

    /* Checks if the client_pipe's name fits in the buffer */
    if (c_path_size > MAX_REQUEST_SIZE - 1) {
        printf("tfs_mount(): client pipe name too big.\n");
        return -1;
    }

    /* Buffer used to send mount commands to the server:
     *  - buffer[0] stores the OP_CODE of the operation
     *  - the rest of the buffer stores the client_pipe's name */
    char buffer[MAX_REQUEST_SIZE];

    /* TODO: review the op_code storage */
    buffer[0] = (char) TFS_OP_CODE_MOUNT;
    strcpy(buffer + 1, client_pipe_path);
    buffer[c_path_size] = 0;

    if (mkfifo(client_pipe_path, 0777) != 0) {
        return -1;
    }

    /* Opens the server for writing. */
    server_fd = open(server_pipe_path, O_WRONLY);
    if (server_fd == -1) {
        unlick(client_pipe_path);
        return -1;
    }

    /* TODO: check the buffer size in write(...) c_path_size + 1 */
    if (write(server_fd, buffer, MAX_REQUEST_SIZE) != 0) {
        close(server_fd);
        unlink(client_pipe_path);
        return -1;
    }

    /* TODO: do we need to close the server? And if so how do we deal with the possible close() error? (-1) */

    client_fd = open(client_pipe_path, O_RDONLY);
    if (client_fd == -1) {
        close(server_fd);
        unlink(client_pipe_path);
        return -1;
    }

    if (read(client_fd, &curr_session_id, sizeof(int)) != 0) {
        close(server_fd);
        close(client_fd);
        unlink(client_pipe_path);
        return -1;
    }

    return 0;
}

int tfs_unmount() {
    /* Buffer used to send unmount commands to the server:
     *  - buffer[0] stores the OP_CODE of the operation
     *  - the rest of the buffer stores the current client's session_id */
    char buffer[MAX_REQUEST_SIZE];

    buffer[0] = (char) TFS_OP_CODE_UNMOUNT;
    /* Stores the current client's session id in the buffer */
    int *session_id = (int*)(&buffer[1]);
    *session_id = curr_session_id;

    if (write(server_fd, buffer, MAX_REQUEST_SIZE) != 0) {
        close(server_fd);
        return -1;
    }

    // read(client_fd, nome do pipe, ...)
    // close(client)
    // unlink(nome do pipe)

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
