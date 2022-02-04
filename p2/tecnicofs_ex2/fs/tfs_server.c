#include "operations.h"

/* Extra includes */
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>


/* Session ID table */
static client_session_t session_id_table[MAX_SERVER_SESSIONS];
static char free_session_table[MAX_SERVER_SESSIONS];

/* Session's thread table */
static pthread_t session_thread_table[MAX_SERVER_SESSIONS];

/* Counter used to keep track of how many active sessions the server is handling */
static int active_session_counter;

/* Cond variable used to control the number of requests taken in 
 * by the server */
static pthread_cond_t request_cond;
static pthread_mutex_t server_mutex;

static pthread_cond_t shutdown_cond;

static inline bool valid_session_id(int session_id) {
    return session_id >= 0 && session_id < MAX_SERVER_SESSIONS;
}


/* 
 * Makes sure "write()" actually writes all the bytes the user requested
 * Inputs:
 *  - file descriptor to write to
 *  - source of the data to write
 *  - size of the data
 * Returns: 0 if successful, -1 otherwise
 */
int write_until_success(int fd, char const *source, size_t size) {
    int wr;
    while ((wr = write(fd, source, size)) != size && errno == EINTR) {
        /* Nothing to do */
    }
    /* If even after the cycle write() hasn't written all the bytes
     * we want, -1 is returned */
    if (wr != size) {
        return -1;
    }
    return 0;
}


/* 
 * Makes sure "read()" actually reads all the bytes the user requested
 * Inputs:
 *  - file descriptor to read from
 *  - destination of the content read
 *  - size of the content
 * Returns: 0 if successful, -1 otherwise
 */
int read_until_success(int fd, char *destination, size_t size) {
    int offset = 0, rd;
    /* TODO: fd + offset maybe? */
    while ((rd = read(fd, destination + offset, size)) != size && errno == EINTR) {
        /* Updates the current offset */
        offset += rd;
    }
    /* If even after the cycle, read() hasn't read all the bytes
     * we want, -1 is returned */
    if (rd != size) {
        return -1;
    }
    return 0;
}


/*
 * Makes sure "open()" actually opens the pipe given
 * Inputs:
 *  - path to the pipe
 * Returns: file descriptor of the pipe if successful, -1 otherwise
 */
int open_until_success(char const *pipe_path, int oflag) {
    int fd;
    while ((fd = open(pipe_path, oflag)) == -1 && errno == EINTR) {
        /* Nothing to do */
    }
    /* Returns the current fd, if -1 it will be dealt with later */
    return fd;
}


/*
 * Makes sure "close()" actually closes the file given
 * Inputs:
 *  - file descriptor to close
 * Returns: 0 if successful, -1 otherwise
 */
int close_until_success(int const fd) {
    int x;
    while ((x = close(fd)) == -1 && errno == EINTR) {
        /* Nothing to do */
    }
    return x;
}


/*
 * Initilizes the server's table and pipe
 * Inputs:
 *  - server pipe's path name
 */
void tfs_server_init(char const *server_pipe_path) {
    if (tfs_init() != 0) {
        perror("[ERR]: ");
        exit(1);
    }

    if (pthread_cond_init(&request_cond, NULL) != 0) {
        perror("[ERR]: ");
        exit(1);
    }

    if (pthread_cond_init(&shutdown_cond, NULL) != 0) {
        pthread_cond_destroy(&request_cond);
        perror("[ERR]: ");
        exit(1);
    }

    if (pthread_mutex_init(&server_mutex, NULL) != 0) {
        pthread_cond_destroy(&request_cond);
        pthread_cond_destroy(&shutdown_cond);
        perror("[ERR]: ");
        exit(1);
    }


    if (mkfifo(server_pipe_path, 0777) != 0) {
        pthread_cond_destroy(&request_cond);
        pthread_cond_destroy(&shutdown_cond);
        pthread_mutex_destroy(&server_mutex);
        perror("[ERR]: ");
        exit(1);
    }

    /* In the beggining there are no active server sessions */
    active_session_counter = 0;

    for (size_t i = 0; i < MAX_SERVER_SESSIONS; i++) {
        free_session_table[i] = FREE;
    }
}


/*
 * Destroys the Server's state
 * Inputs:
 *  - server's file descriptor
 */
void tfs_server_destroy(int server_fd) {
    if (pthread_cond_destroy(&request_cond) != 0) {
        perror("[ERR]: ");
        exit(1);
    }

    if (pthread_mutex_destroy(&server_mutex) != 0) {
        perror("[ERR]: ");
        exit(1);
    }

    for (size_t i = 0; i < MAX_SERVER_SESSIONS; i++) {
        /* TODO: maybe unlink the clients? */
        if (close_until_success(session_id_table[i].client_fd) != 0) {
            perror("[ERR]: ");
            exit(1);
        }
    }

    if (close_until_success(server_fd) != 0) {
        perror("[ERR]: ");
        exit(1);
    }
}


/*
 * Allocates a new entry for the current session
 * Returns: entry index if successful, -1 otherwise
 */
int session_id_alloc() {
    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    for (int i = 0; i < MAX_SERVER_SESSIONS; i++) {
        if (free_session_table[i] == FREE) {
            
            free_session_table[i] = TAKEN;

            if (pthread_mutex_unlock(&server_mutex) != 0) {
                return -1;
            }
            return i;
        }
    }
    pthread_mutex_unlock(&server_mutex);
    return -1;
}


/*
 * Returns a pointer to an existing client session.
 * Input:
 *  - session_id: session's identifier
 * Returns: pointer if successful, NULL if failed
 */
client_session_t *session_get(int session_id) {
    if (!valid_session_id(session_id)) {
        return NULL;
    }

    return &session_id_table[session_id];
}


/*
 * Frees an entry from the session_id table
 * Inputs:
 *  - closing session's id
 * Returns: 0 if succsessful, -1 otherwise
 */
int session_id_remove(int session_id) {
    if (!valid_session_id(session_id)) {
        return -1;
    }

    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    free_session_table[session_id] = FREE;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }

    return 0;
}


/* 
 * Sends a message (int) to a certain client
 * Inputs:
 *  - file to write the message to
 *  - value to be sent
 * Returns: 0 if successful, -1 otherwise
 */
int send_message(int dest_fd, int ret) {
    if (write_until_success(dest_fd, &ret, RETURN_VAL_SIZE) != 0) {
        return -1;
    }
    return 0;
}


/* 
 * Handles Mount requests from the clients
 * Inputs:
 *  - arguments for mount
 * In case of an error, notifies the client
 */
void tfs_server_mount(void const *arg) {
    char *args = (char*) arg;

    /* Gets the argument we need for the mount command:
     * <client pipe path name> */
    char client_pipe_path[MAX_CPATH_LEN];
    strcpy(client_pipe_path, args);

    /* Opens the client's pipe for every future witing */
    int client_fd = open_until_success(client_pipe_path, O_WRONLY);
    if (client_fd == -1) {
        /* TODO: exit? */
        exit(1);
    }

    int session_id = session_id_alloc();

    if (pthread_mutex_lock(&server_mutex) != 0) {
        exit(1);
    }

    /* If session_id == -1 it means there is no more space for any more requests
     * to the server (atleast for now) and so we wait for an empty entry to appear */
    if (session_id == -1) {
        if (pthread_cond_wait(&request_cond, &server_mutex) != 0) {
            exit(1);
        }
    }

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        exit(1);
    }

    /* Writes to the client's pipe its session id */
    if (write_until_success(client_fd, session_id, SESSION_ID_SIZE) != 0) {
        if (send_message(client_fd, -1) != 0) {
            exit(1);
        }
        return;
    }

    active_session_counter++;

    if (pthread_mutex_lock(&server_mutex) != 0) {
        exit(1);
    }

    /* Fills the structs's fields with the client's information */
    session_id_table[session_id].client_fd = client_fd;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
       exit(1);
    }
}


/* 
 * Handles Unmount requests from the clients
 * Inputs:
 *  - arguments for unmount
 * In case of an error, notifies the client
 */
void tfs_server_unmount(void const *arg) {
    char *args = (char*) arg;

    /* Gets the argument we need for the unmount command:
     * <client's session id> */
    int session_id = *(int*) args[0];

    /* Protect the session_get function call */
    if (pthread_mutex_lock(&server_mutex) != 0) {
        exit(1);
    }

    client_session_t *client_session = session_get(session_id);
    if (client_session == NULL) {
        /* TODO: review this */
        exit(1);
    }

    int client_fd = client_session->client_fd;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        exit(1);
    }
    
    /* Removes the session from the system */
    if (session_id_remove(session_id) != 0) {
        if (send_message(client_fd, -1) != 0) {
            exit(1);
        }
        return;
    }

    /* If the client recieves a 0 through the pipe, unmount was successful */
    int success = 0;
    /* Sends to the client its pipe's name (for unlinking by the client) */
    if (write_until_success(client_fd, &success, RETURN_VAL_SIZE) != 0) {
        if (send_message(client_fd, -1) != 0) {
            exit(1);
        }
        return;
    }

    if (pthread_mutex_lock(&server_mutex) != 0) {
        exit(1);
    }

    active_session_counter--;

    if (active_session_counter < MAX_SERVER_SESSIONS) {
        pthread_cond_signal(&request_cond);
    }

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        exit(1);
    }
}


/* 
 * Handles Open requests from the clients
 * Inputs:
 *  - arguments for open
 * In case of an error, notifies the client
 */
void tfs_server_open(void const *arg) {
    char *args = (char*) arg;

    /* Gets the arguments we need for the open command:
     * <client's session id> | <file name> | flags */
    int session_id = *(int*) args[0];
    char file_name[MAX_CPATH_LEN];
    strcpy(file_name, args + SESSION_ID_SIZE);
    int flags = *(int*) (args + SESSION_ID_SIZE + MAX_CPATH_LEN);

    /* Protect the session_get function call */
    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    client_session_t *client_session = session_get(session_id);
    if (client_session == NULL) {
        return -1;
    }

    /* TODO: lock session */
    int client_fd = client_session->client_fd;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }

    /* Stores the return value of tfs_open() */
    int ret;
    ret = tfs_open(file_name, flags);

    /* If for some reason tfs_close() returns -1, it won't be a problem for now,
     * as the client will deal with it accordingly */
    if (write_until_success(client_fd, ret, RETURN_VAL_SIZE) != 0) {
        if (send_message(client_fd, -1) != 0) {
            exit(1);
        }
        return;
    }
}


int tfs_server_close(void const *arg) {
    char *args = (char*) arg;

    /* Gets the arguments we need for the close command:
     * session_id | fhandle */
    int session_id = *(int*) args[0];
    int fhandle = *(int*) (args + SESSION_ID_SIZE);

    /* Protect the session_get function call */
    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    client_session_t *client_session = session_get(session_id);
    if (client_session == NULL) {
        return -1;
    }

    /* TODO: lock session */
    int client_fd = client_session->client_fd;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }

    /* Stores the return value of tfs_close() */
    int ret;
    ret = tfs_close(fhandle);

    /* If for some reason tfs_close() returns -1, it won't be a problem for now,
     * as the client will deal with it accordingly */
    if (write_until_success(client_fd, ret, RETURN_VAL_SIZE) != 0) {
        if (send_message(client_fd, -1) != 0) {
            exit(1);
        }
        return;
    }
}


int tfs_server_write(void const *arg) {
    char *args = (char*) arg;

    /* Gets the arguments we need for the write command:
     * session_id | fhandle | len | <data to write> */
    int session_id = *(int*) args[0];
    int fhandle = *(int*) (args + SESSION_ID_SIZE);
    int len = *(int*) (args + SESSION_ID_SIZE + FHANDLE_SIZE);
    char to_write[len];
    strcpy(to_write, args + SESSION_ID_SIZE + FHANDLE_SIZE + len);

    /* Protect the session_get function call */
    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    client_session_t *client_session = session_get(session_id);
    if (client_session == NULL) {
        return -1;
    }

    /* TODO: lock session */
    int client_fd = client_session->client_fd;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }

    /* Stores the return value of tfs_close() */
    int ret;
    ret = tfs_write(fhandle, to_write, len);

    /* If for some reason tfs_write() returns -1, it won't be a problem for now,
     * as the client will deal with it accordingly */
    if (write_until_success(client_fd, ret, RETURN_VAL_SIZE) != 0) {
        if (send_message(client_fd, -1) != 0) {
            exit(1);
        }
        return;
    }
}


int tfs_server_read(void const *arg) {
    char *args = (char*) arg;

    /* Gets the arguments we need for the read command:
     * session_id | fhandle | len */
    int session_id = *(int*) args[0];
    int fhandle = *(int*) (args + SESSION_ID_SIZE);
    int len = *(int*) (args + SESSION_ID_SIZE + FHANDLE_SIZE);

    /* Protect the session_get function call */
    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    client_session_t *client_session = session_get(session_id);
    if (client_session == NULL) {
        return -1;
    }

    /* TODO: lock session */
    int client_fd = client_session->client_fd;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }
    
    char read[len];
    /* Stores the return value of tfs_read() */
    int ret;
    ret = tfs_read(fhandle, read, len);

    /* If for some reason tfs_read() returns -1, it won't be a problem for now,
     * as the client will deal with it accordingly */
    if (write_until_success(client_fd, ret, RETURN_VAL_SIZE) != 0) {
        if (send_message(client_fd, -1) != 0) {
            exit(1);
        }
        return;
    }
}


int tfs_server_shutdown(void const *arg) {
    char *args = (char*) arg;

    /* Gets the arguments we need for the read command:
     * session_id | fhandle */
    int session_id = *(int*) args[0];
    int fhandle = *(int*) (args + SESSION_ID_SIZE);

    /* Protect the session_get function call */
    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    client_session_t *client_session = session_get(session_id);
    if (client_session == NULL) {
        return -1;
    }

    /* TODO: lock session */
    int client_fd = client_session->client_fd;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }

    /* Stores the return value of tfs_destroy_after_all_closed() */
    int ret;
    ret = tfs_shutdown_after_all_closed();

    /* If for some reason tfs_destroy_after_all_closed() returns -1, it won't 
     * be a problem for now, as the client will deal with it accordingly */
    if (write_until_success(client_fd, ret, RETURN_VAL_SIZE) != 0) {
        if (send_message(client_fd, -1) != 0) {
            exit(1);
        }
        return;
    }
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

    int server_fd;

    /* Opens the server's pipe for future reading */
    server_fd = open_until_success(pipename, O_RDONLY);
    if (server_fd == -1) {
        return -1;
    }

    /* The server will run indefinitely, waiting for requests from the clients */
    while(1) {
        /* Buffer that stores request's fields (OP_CODE + rest of the fields) */
        char request_buffer[MAX_REQUEST_SIZE];

        int offset = 0, rd;
        while ((rd = read(server_fd, request_buffer + offset, MAX_REQUEST_SIZE)) != MAX_REQUEST_SIZE && errno == EINTR) {
            /* If read returns 0, we "reboot" the server */
            if (rd == 0) {
                if (close_until_success(server_fd) != 0) {
                    exit(1);
                }
                if ((server_fd = open_until_success(pipename, O_RDONLY)) != 0) {
                    exit(1);
                }
            }
            /* Updates the current offset */
            offset += rd;
        }

        char op_code = request_buffer[0];

        switch(op_code) {
            /* "request_buffer+1" is used to skip the OP_CODE */

            case 1:
                tfs_server_mount(request_buffer+1);
            
            case 2:
                tfs_server_unmount(request_buffer+1);
            
            case 3:
                tfs_server_open(request_buffer+1);

            case 4:
                tfs_server_close(request_buffer+1);

            case 5:
                tfs_server_write(request_buffer+1);

            case 6:
                tfs_server_read(request_buffer+1);

            case 7:
                tfs_server_shutdown(request_buffer+1);
        }
    }

    return 0;
}