#ifndef COMMON_H
#define COMMON_H

/* Maximum number of sessions to the server */
#define MAX_SERVER_SESSIONS (1)

/* Maximum size of the request buffer 
 * The first index contains the OP_CODE and the rest of the buffer
 * stores the rest of the information needed for the operation */
#define MAX_REQUEST_SIZE (41)


typedef struct {
    /* Client pipe path's name */
    char path_name[MAX_REQUEST_SIZE];

    /* Clients pipe file descriptor */
    int client_fd;
} client_session_t;


/* tfs_open flags */
enum {
    TFS_O_CREAT = 0b001,
    TFS_O_TRUNC = 0b010,
    TFS_O_APPEND = 0b100,
};

/* operation codes (for client-server requests) */
enum {
    TFS_OP_CODE_MOUNT = 1,
    TFS_OP_CODE_UNMOUNT = 2,
    TFS_OP_CODE_OPEN = 3,
    TFS_OP_CODE_CLOSE = 4,
    TFS_OP_CODE_WRITE = 5,
    TFS_OP_CODE_READ = 6,
    TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED = 7
};

#endif /* COMMON_H */