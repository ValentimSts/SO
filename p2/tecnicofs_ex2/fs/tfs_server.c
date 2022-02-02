#include "operations.h"

/* Extra includes */
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>


/* Session ID table */
static client_session_t session_id_table[MAX_SERVER_SESSIONS];
static char free_session_table[MAX_SERVER_SESSIONS];

/* Counter used to keep track of how many active sessions the server is handling */
static int active_session_counter;

/* Cond variable used to control the number of requests taken in 
 * by the server */
static pthread_cond_t request_cond;
static pthread_mutex_t server_mutex;


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
    /* If even after the cycle read() hasn't read all the bytes
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
    if (pthread_cond_init(&request_cond, NULL) != 0) {
        perror("[ERR]: ");
        exit(1);
    }

    if (pthread_mutex_init(&server_mutex, NULL) != 0) {
        pthread_cond_destroy(&request_cond);
        perror("[ERR]: ");
        exit(1);
    }

    if (mkfifo(server_pipe_path, 0777) != 0) {
        pthread_cond_destroy(&request_cond);
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

    for (size_t i = 0; i < MAX_SERVER_SESSIONS; i++) {
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
 * Handles Mount requests from the clients
 * Inputs:
 *  - arguments for mount (client pipe path)
 * Returns: 0 if successful, -1 otherwise
 */
int tfs_server_mount(char const *args) {
    /* Concatenates the args given, allowing us to only use what is
     * necessary (client pipe path) */
    char client_pipe_path[MAX_CPATH_LEN];
    strcpy(client_pipe_path, args);

    int session_id = session_id_alloc();

    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    /* TODO: while or if? */
    /* If session_id == -1 it means there is no more space for any more requests
     * to the server (atleast for now) and so we wait for an empty entry to appear */
    if (session_id == -1) {
        if (pthread_cond_wait(&request_cond, &server_mutex) != 0) {
            return -1;
        }
    }

    active_session_counter++;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }

    /* Opens the client's pipe for evry future witing */
    int client_fd = open_until_success(client_pipe_path, O_WRONLY);
    if (client_fd == -1) {
        return -1;
    }

    /* Writes to the client's pipe its session id */
    if (write_until_success(client_fd, session_id, sizeof(int)) != 0) {
        close_until_success(client_fd);
        return -1;
    }

    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    /* Fills the structs's fields with the client's information */
    strcpy(session_id_table[session_id].path_name, client_pipe_path);
    session_id_table[session_id].client_fd = client_fd;

    if (pthread_mutex_lock(&server_mutex) != 0) {
       return -1;
    }


    return 0;
}


/* 
 * Handles Unmount requests from the clients
 * Inputs:
 *  - arguments for unmount (client session id)
 * Returns 0 if successful, -1 otherwise
 */
int tfs_server_unmount(char const *args) {
    /* TODO: review this please... */
    int session_id = (int) args[0];

    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    int client_fd = session_id_table[session_id].client_fd;

    char cpath_name[MAX_CPATH_LEN];
    /* TODO: uhhhhh? */
    strcpy(cpath_name, session_id_table[session_id].path_name);

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }
    
    session_id_remove(session_id);
    
    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    active_session_counter--;

    /* Writes to the client's pipe its pipe's name (for unlinking by the client) */
    if (write_until_success(client_fd, cpath_name, MAX_CPATH_LEN) != 0) {
        close_until_success(client_fd);
        return -1;
    }

    if (active_session_counter < MAX_SERVER_SESSIONS) {
        pthread_cond_signal(&request_cond);
    }

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }

    return 0;
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

    int server_fd;

    /* Opens the server's pipe for every future reading */
    server_fd = open_until_success(pipename, O_RDONLY);
    if (server_fd == -1) {
        return -1;
    }

    /* The server will run indefinitely, waiting for requests from the clients */
    while(1) {
        /* Buffer that stores request's fields (OP_CODE + rest of the fields) */
        char request_buffer[MAX_REQUEST_SIZE];

        if (read_until_success(server_fd, request_buffer, MAX_REQUEST_SIZE) != 0) {
            close_until_success(server_fd);
            /* open(server) again? */
            return -1;
        }

        char op_code = request_buffer[0];

        switch(op_code) {
            case 1:
                tfs_server_mount(request_buffer + 1);
            
            case 2:
                tfs_server_unmount(request_buffer);
            
            case 3:
                tfs_server_open();

            case 4:
                tfs_server_close();

            case 5:
                tfs_server_write();

            case 6:
                tfs_server_read();

            case 7:
                tfs_server_shutdown();
        }

    }

    return 0;
}

