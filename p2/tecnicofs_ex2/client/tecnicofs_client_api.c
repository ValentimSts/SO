#include "tecnicofs_client_api.h"

#include <stdio.h>

/* Global variables used to store both the server and clients's file descriptors */
static int client_fd;
static int server_fd;

/* Stores the current client session id. Returned by the server after
 * each successfull command */
static int curr_session_id;

/* Stores the current client's pipe path name (for later unlink) */
static char curr_client_pipe_path[MAX_CPATH_LEN];


int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    size_t buffer_size = OP_CODE_SIZE + MAX_CPATH_LEN;
    /* Stores the current path */
    strcpy(curr_client_pipe_path, client_pipe_path);

    /* Buffer used to send mount commands to the server:
     * - Structure:
     *   OP_CODE | <client pipe path name> */
    char buffer[buffer_size];

    buffer[0] = (char) TFS_OP_CODE_MOUNT;
    strcpy(buffer + OP_CODE_SIZE, client_pipe_path);

    if (mkfifo(client_pipe_path, 0777) != 0 && errno != EEXIST) {
        printf("joao1\n");
        return -1;
    }

    printf("client side server\n");
    /* Opens the server's pipe for every future writing */
    server_fd = open_until_success(server_pipe_path, O_WRONLY);
    if (server_fd == -1) {
        unlink(client_pipe_path);
        printf("joao2\n");
        return -1;
    }

    if (write_until_success(server_fd, buffer, buffer_size) != 0) {
        close_until_success(server_fd);
        unlink(client_pipe_path);
        printf("joao3\n");
        return -1;
    }

    printf("client side client\n");
    /* Opens the client's pipe for every future reading (in the same session) */
    client_fd = open_until_success(client_pipe_path, O_RDONLY);
    if (client_fd == -1) {
        close_until_success(server_fd);
        unlink(client_pipe_path);
        printf("joao4\n");
        return -1;
    }

    if (read_until_success(client_fd, &curr_session_id, SESSION_ID_SIZE) != 0) {
        close_until_success(server_fd);
        close_until_success(client_fd);
        unlink(client_pipe_path);
        printf("joao5\n");
        return -1;
    }

    /* In case the server sent a -1 to the client, an error ocurred on the
     * server's side, and so, we return error */
    if (curr_session_id == -1) {
        printf("joao6\n");
        return -1;
    }

    return 0;
}

int tfs_unmount() {
    size_t buffer_size = OP_CODE_SIZE + SESSION_ID_SIZE;

    /* Buffer used to send unmount commands to the server:
     * - Structure:
     *   OP_CODE | session_id */
    char buffer[buffer_size];

    buffer[0] = (char) TFS_OP_CODE_UNMOUNT;
    memcpy(buffer + OP_CODE_SIZE, &curr_session_id, SESSION_ID_SIZE);

    if (write_until_success(server_fd, buffer, buffer_size) != 0) {
        return -1;
    }
    
    /* Stores the client's pipe path name, sent by the server to the client */
    int ret;
    if (read_until_success(client_fd, &ret, RETURN_VAL_SIZE) != 0) {
        return -1;
    }

    /* In case the server sent a -1 to the client, an error ocurred on the
     * server's side, and so, we return error */
    if (ret == -1) {
        return -1;
    }

    /* Closes the client's pipe */
    if (close_until_success(client_fd) != 0) {
        return -1;
    }

    /* Deletes the client's pipe */
    if (unlink(curr_client_pipe_path) != 0) {
        return -1;
    }

    /* TODO: close the server pipe? */
    if (close_until_success(server_fd) != 0) {
        return -1;
    }

    return 0;
}


int tfs_open(char const *name, int flags) {
    /* Size of the buffer used. Since "name" has a maximum size of 40 we use the same
     * macro as the one used for the client_pipe_path length */
    size_t buffer_size = OP_CODE_SIZE + SESSION_ID_SIZE + MAX_CPATH_LEN + FLAG_SIZE;

    /* Buffer used to send open commands to the server
     * - Structure:
     *   OP_CODE | session_id | <file name> | flags */
    char buffer[buffer_size];

    buffer[0] = (char) TFS_OP_CODE_OPEN;
    memcpy(buffer + OP_CODE_SIZE, &curr_session_id, SESSION_ID_SIZE);
    strcpy(buffer + OP_CODE_SIZE + SESSION_ID_SIZE, name);
    memcpy(buffer + OP_CODE_SIZE + SESSION_ID_SIZE + MAX_CPATH_LEN, &flags, FLAG_SIZE);

    if (write_until_success(server_fd, buffer, buffer_size) != 0) {
        return -1;
    }
    
    int ret;
    if (read_until_success(client_fd, &ret, RETURN_VAL_SIZE) != 0) {
        return -1;
    }

    /* In case the server sent a -1 to the client, an error ocurred on the
     * server's side, and so, we return error */
    if (ret == -1) {
        return -1;
    }

    return 0;
}


int tfs_close(int fhandle) {
    size_t buffer_size = OP_CODE_SIZE + SESSION_ID_SIZE + FHANDLE_SIZE;

    /* Buffer used to send close commands to the server
     * - Structure:
     *   OP_CODE | session_id | fhandle */
    char buffer[buffer_size];

    buffer[0] = (char) TFS_OP_CODE_OPEN;
    memcpy(buffer + OP_CODE_SIZE, &curr_session_id, SESSION_ID_SIZE);
    memcpy(buffer + OP_CODE_SIZE + SESSION_ID_SIZE, &fhandle, FHANDLE_SIZE);

    if (write_until_success(server_fd, buffer, buffer_size) != 0) {
        return -1;
    }
    
    int ret;
    if (read_until_success(client_fd, &ret, RETURN_VAL_SIZE) != 0) {
        return -1;
    }

    /* In case the server sent a -1 to the client, an error ocurred on the
     * server's side, and so, we return error */
    if (ret == -1) {
        return -1;
    }

    return 0;
}


ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    /* Size of the buffer used (1024 comes from the size of a block) */
    size_t buffer_size = OP_CODE_SIZE + SESSION_ID_SIZE + FHANDLE_SIZE + LEN_SIZE + len;

    /* Buffer used to send write commands to the server
     * - Structure:
     *   OP_CODE | session_id | fhandle | len | <buffer's content> */
    char write_buffer[buffer_size];

    write_buffer[0] = (char) TFS_OP_CODE_OPEN;
    memcpy(write_buffer + OP_CODE_SIZE, &curr_session_id, SESSION_ID_SIZE);
    memcpy(write_buffer + OP_CODE_SIZE + SESSION_ID_SIZE, &fhandle, FHANDLE_SIZE);
    memcpy(write_buffer + OP_CODE_SIZE + SESSION_ID_SIZE + FHANDLE_SIZE, &len, LEN_SIZE);
    memcpy(write_buffer + OP_CODE_SIZE + SESSION_ID_SIZE + FHANDLE_SIZE + LEN_SIZE, buffer, len);

    if (write_until_success(server_fd, write_buffer, buffer_size) != 0) {
        return -1;
    }
    
    ssize_t ret;
    if (read_until_success(client_fd, &ret, RDWR_VAL_SIZE) != 0) {
        return -1;
    }

    /* In case the server sent a -1 to the client, an error ocurred on the
     * server's side, and so, we return error */
    if (ret == -1) {
        return -1;
    }

    return 0;
}


ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    size_t buffer_size = OP_CODE_SIZE + SESSION_ID_SIZE + FHANDLE_SIZE + LEN_SIZE + len;

    /* Buffer used to send read commands to the server
     * - Structure:
     *   OP_CODE | session_id | fhandle | len | <buffer itself used to store the data read> */
    char read_buffer[buffer_size];

    read_buffer[0] = (char) TFS_OP_CODE_OPEN;
    memcpy(read_buffer + OP_CODE_SIZE, &curr_session_id, SESSION_ID_SIZE);
    memcpy(read_buffer + OP_CODE_SIZE + SESSION_ID_SIZE, &fhandle, FHANDLE_SIZE);
    memcpy(read_buffer + OP_CODE_SIZE + SESSION_ID_SIZE + FHANDLE_SIZE, &len, LEN_SIZE);
    memcpy(read_buffer + OP_CODE_SIZE + SESSION_ID_SIZE + FHANDLE_SIZE + LEN_SIZE, buffer, len);

    if (write_until_success(server_fd, read_buffer, buffer_size) != 0) {
        return -1;
    }
    
    ssize_t ret;
    if (read_until_success(client_fd, &ret, RDWR_VAL_SIZE) != 0) {
        return -1;
    }

    /* In case the server sent a -1 to the client, an error ocurred on the
     * server's side, and so, we return error */
    if (ret == -1) {
        return -1;
    }

    return 0;
}


int tfs_shutdown_after_all_closed() {
    size_t buffer_size = OP_CODE_SIZE + SESSION_ID_SIZE;

    /* Buffer used to send shutdown_after_all_closed commands to the server
     * - Structure:
     *   OP_CODE | session_id */
    char buffer[buffer_size];

    buffer[0] = (char) TFS_OP_CODE_OPEN;
    memcpy(buffer + OP_CODE_SIZE, &curr_session_id, SESSION_ID_SIZE);

    if (write_until_success(server_fd, buffer, buffer_size) != 0) {
        return -1;
    }
    
    int ret;
    if (read_until_success(client_fd, &ret, RETURN_VAL_SIZE) != 0) {
        return -1;
    }

    /* TODO: exit? */
    exit(1);
}
