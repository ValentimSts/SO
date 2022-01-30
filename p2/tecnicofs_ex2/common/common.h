#ifndef COMMON_H
#define COMMON_H

/* Maximum number of sessions to the server */
#define MAX_SERVER_SESSIONS (1)

/* Maximum length of the client path name
 * Size 41 to account for the '/0' at the end */
#define MAX_CPATH_LEN (41)

/* Struct used to represent the client's requests to the
 * server. */
typedef struct {
    /* OP_CODE of the operation. */
    int opcode;
    /* Client path name. */
    char c_path_name[MAX_CPATH_LEN];
} c_request;


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