#ifndef COMMON_H
#define COMMON_H

/* Maximum size of the client's pipe path name */
#define MAX_CPATH_LEN (40)
/* Size the op_code takes in the buffer */
#define OP_CODE_SIZE (sizeof(char))
/* Size the session id takes in the buffer */
#define SESSION_ID_SIZE (sizeof(int))
/* Size the fhandle takes in the buffer */
#define FHANDLE_SIZE (sizeof(int))
/* Size the len takes in the buffer */
#define LEN_SIZE (sizeof(size_t))
/* Size of the return values sent to the clients by the server
 * as confirmation that the command as been executed */
#define RETURN_VAL_SIZE (sizeof(int))


/* TODO: check where to put the defines/structs here or "config.h" */


/* Maximum number of sessions to the server */
#define MAX_SERVER_SESSIONS (1)

#define MAX_REQUEST_SIZE (OP_CODE_SIZE + SESSION_ID_SIZE + FHANDLE_SIZE + LEN_SIZE + 1024)

typedef struct {
    /* Clients pipe file descriptor */
    int client_fd;

    /* client's mutex / cond var */
    /*
    pthread_mutex_t client_mutex;
    pthread_cond_t client_cond;
    */

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