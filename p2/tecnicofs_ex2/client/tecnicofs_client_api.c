#include "tecnicofs_client_api.h"

/* Extra includes */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>


/* Global variables used to store both the server and clients's file descriptors */
static int client_fd;
static int server_fd;

/* Stores the current client session fd. Returned by the server after
 * each successfull command */
static int curr_session_fd;


int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    int buffer_size = OP_CODE_SIZE + MAX_CPATH_LEN;

    int c_path_size = strlen(client_pipe_path);

    /* Checks if the client_pipe's name fits in the buffer */
    if (c_path_size > MAX_CPATH_LEN - 1) {
        printf("tfs_mount(): client pipe name too big.\n");
        return -1;
    }

    /* Buffer used to send mount commands to the server:
     * - buffer[0] stores the OP_CODE of the operation
     * - the rest of the buffer stores the client_pipe's name */
    char buffer[buffer_size];

    buffer[0] = (char) TFS_OP_CODE_MOUNT;
    /* TODO: strncat? */
    strcpy(buffer + OP_CODE_SIZE, client_pipe_path);
    buffer[c_path_size] = '\0';

    if (mkfifo(client_pipe_path, 0777) != 0) {
        return -1;
    }

    /* TODO: Server pipe already open? */
    /* Opens the server for writing. */
    server_fd = open(server_pipe_path, O_WRONLY);
    if (server_fd == -1) {
        unlick(client_pipe_path);
        return -1;
    }

    if (write(server_fd, buffer, buffer_size) != 0) {
        close(server_fd);
        unlink(client_pipe_path);
        return -1;
    }


    client_fd = open(client_pipe_path, O_RDONLY);
    if (client_fd == -1) {
        close(server_fd);
        unlink(client_pipe_path);
        return -1;
    }

    if (read(client_fd, &curr_session_fd, sizeof(int)) != 0) {
        close(server_fd);
        close(client_fd);
        unlink(client_pipe_path);
        return -1;
    }

    return 0;
}

int tfs_unmount() {
    int buffer_size = OP_CODE_SIZE + sizeof(int);

    /* Buffer used to send unmount commands to the server:
     * - buffer[0] stores the OP_CODE of the operation
     * - the rest of the buffer stores the current client's session_id */
    char buffer[buffer_size];

    buffer[0] = (char) TFS_OP_CODE_UNMOUNT;
    /* Stores the current client's session id in the buffer */
    int *session_id = (int*)(&buffer[OP_CODE_SIZE]);
    *session_id = curr_session_fd;

    if (write(server_fd, buffer, buffer_size) != 0) {
        close(server_fd);
        return -1;
    }

    
    char cpipe_name[MAX_CPATH_LEN];

    // if (read(client_fd, nome do pipe, ...)
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
