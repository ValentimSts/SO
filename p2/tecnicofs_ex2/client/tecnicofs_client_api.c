#include "tecnicofs_client_api.h"

/* Extra includes */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>


/* Global variables used to store both the server and clients's file descriptors */
static int client_fd;
static int server_fd;

/* Stores the current client session id. Returned by the server after
 * each successfull command */
static int curr_session_id;


/* 
 * Makes sure "write()" actually writes all the bytes the user requested
 * Inputs:
 *  - file descriptor to write to
 *  - source of the content to write
 *  - size of the content
 * 
 * Returns: 0 if successful, -1 otherwise
 */
int write_until_success(int fd, char const *source, size_t size) {
    int offset = 0, wr;
    while (offset != size) {
        if ((wr = write(fd, source + offset, size)) == -1 && errno != EINTR) {
            return -1;
        }
        /* Updates the current offset */
        offset += wr;
    }
    return 0;
}


/* 
 * Makes sure "read()" actually reads all the bytes the user requested
 * Inputs:
 *  - file descriptor to read from
 *  - destination of the content read
 *  - size of the content
 * 
 * Returns: 0 if successful, -1 otherwise
 */
int read_until_success(int fd, char *destination, size_t size) {
    int offset = 0, rd;
    while (offset != size) {
        if ((rd = read(fd, destination + offset, size)) == -1 && errno != EINTR) {
            return -1;
        }
        /* Updates the current offset */
        offset += rd;
    }
    return 0;
}


int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    int buffer_size = OP_CODE_SIZE + MAX_CPATH_LEN;

    /* TODO: check if this is really needed, since the truncated name will work either way */
    int c_path_size = strlen(client_pipe_path);
    /* Checks if the client_pipe's name fits in the buffer */
    if (c_path_size > MAX_CPATH_LEN - 1) {
        return -1;
    }

    /* Buffer used to send mount commands to the server:
     * - buffer[0] stores the OP_CODE of the operation
     * - the rest of the buffer stores the client_pipe's name */
    char buffer[buffer_size];

    buffer[0] = (char) TFS_OP_CODE_MOUNT;
    strcpy(buffer + OP_CODE_SIZE, client_pipe_path);

    if (mkfifo(client_pipe_path, 0777) != 0) {
        return -1;
    }

    /* Opens the server's pipe for every future writing */
    server_fd = open(server_pipe_path, O_WRONLY);
    if (server_fd == -1) {
        unlick(client_pipe_path);
        return -1;
    }

    if (write_until_success(server_fd, buffer, buffer_size) != 0) {
        close(server_fd);
        unlink(client_pipe_path);
        return -1;
    }

    /* Opens the client's pipe for every future reading (in the same session) */
    client_fd = open(client_pipe_path, O_RDONLY);
    if (client_fd == -1) {
        close(server_fd);
        unlink(client_pipe_path);
        return -1;
    }

    if (read_until_success(client_fd, &curr_session_id, sizeof(int)) != 0) {
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
    memcpy(buffer + OP_CODE_SIZE, &curr_session_id, sizeof(int));

    if (write_until_success(server_fd, buffer, buffer_size) != 0) {
        return -1;
    }
    
    /* Stores the client's pipe path name, sent by the server to the client */
    char cpipe_name[MAX_CPATH_LEN];
    if (read_until_success(client_fd, cpipe_name, MAX_CPATH_LEN) != 0) {
        return -1;
    }

    /* Closes the client's pipe */
    if (close(client_fd) != 0) {
        return -1;
    }

    /* Deletes the named pipe */
    if (unlink(cpipe_name) != 0) {
        return -1;
    }

    /* TODO: close the server pipe? */
    /*
    if (close(server_fd) != 0) {
        return -1;
    }
    */

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
