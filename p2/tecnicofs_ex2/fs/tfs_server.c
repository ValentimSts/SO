#include "operations.h"

/* Extra includes */
#include <fcntl.h>

static int session_id_table[MAX_SERVER_SESSIONS];

/* 
 * Initilizes the server's table and pipe
 * Returns: 0 if successful, -1 otherwise
 */
int tfs_server_init(char const *server_pipe_path) {
    if (mkfifo(server_pipe_path, 0777) == -1) {
        return -1;
    }

    for (size_t i = 0; i < MAX_SERVER_SESSIONS; i++) {
        session_id_table[i] = -1;
    }
    return 0;
}

/*
 * Destroys the Server's state
 */
void tfs_server_destroy(int server_fd) {
    for (size_t i = 0; i < MAX_SERVER_SESSIONS; i++) {
        close()
    }
}

/*
 * Allocates a new block for the current session
 * Returns: block index if successful, -1 otherwise
 */
int session_id_alloc() {
    for (size_t i = 0; i < MAX_SERVER_SESSIONS; i++) {
        if (session_id_table[i] == -1) {
            return i;
        }
    }
    return -1;
}

/*
 * Frees an entry from the session_id table
 * Inputs:
 *  - session's id to close
 */
void session_id_remove(int session_id) {
    session_id_table[session_id] = -1;
}


void tfs_server_mount(char const *client_pipe_path) {
    int session_id = session_id_alloc();
    if (session_id == -1) {
        printf("tfs_server_mount(): [ERR] ")
    }

    /* Writes to the client's pipe its session id */
    int client_id = open(client_pipe_path, O_WRONLY);

    /* We don't check the return value of open as */
    if (client_id != -1) {
        /* TODO: do we need a loop until write works? */
        write(client_id, session_id, sizeof(int));
        close(client_id);
        session_id_table[session_id] = client_id;
    }
}


int tfs_server_unmount() {
    // implement
}


int tfs_server_open() {
    // implement
}


int tfs_server_close() {
    // implement
}


int tfs_server_write() {
    // implement
}


int tfs_server_read() {
    // implement
}


int tfs_server_shutdown() {
    // implement
}


int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    /* Initialize the server */
    server_init(pipename);

    /* Server's file descriptor */
    int server_fd;

    /* The server will run indefinitely, waiting for requests from the clients */
    while(1) {
        /* Buffer that stores request's fields (OP_CODE and client_pipe_path name) */
        char request_buffer[MAX_CPATH_LEN];

        server_fd = open(pipename, O_RDONLY);
        if (server_fd == -1) {
            return -1;
        }

        if (read(server_fd, request_buffer, MAX_CPATH_LEN) == -1) {
            close(server_fd);
            return -1;
        }

        char op_code = request_buffer[0];

        switch(op_code) {
            case 1:
                server_mount(request_buffer + 1);
            
            case 2:

        }

    }

    return 0;
}