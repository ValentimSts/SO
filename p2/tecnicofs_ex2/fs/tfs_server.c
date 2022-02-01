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
 *  - source of the content to write
 *  - size of the content
 * 
 * Returns: 0 if successful, -1 otherwise
 */
int write_until_success(int fd, char const *source, size_t size) {
    int offset = 0, wr;
    while ((wr = write(fd, source + offset, size)) != size && errno != EINTR) {
        if (wr == -1) {
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
    while ((rd = read(fd, destination + offset, size)) != size && errno != EINTR) {
        if (rd == -1) {
            return -1;
        }
        /* Updates the current offset */
        offset += rd;
    }
    return 0;
}

/*
 * Initilizes the server's table and pipe
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
        if (close(session_id_table[i]) != 0) {
            perror("[ERR]: ");
            exit(1);
        }
    }

    if (close(server_fd) != 0) {
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
 *  - client pipe path name
 * Returns: 0 if successful, -1 otherwise
 */
int tfs_server_mount(char const *client_pipe_path) {
    /* TODO: fields as argument? */

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
    int client_fd = open(client_pipe_path, O_WRONLY);
    if (client_fd == -1) {
        return -1;
    }

    /* Writes to the client's pipe its session id */
    if (write_until_success(client_fd, session_id, sizeof(int)) != 0) {
        close(client_fd);
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
 *  - client session id
 * Returns 0 if successful, -1 otherwise
 */
int tfs_server_unmount(int session_id) {
    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    int client_fd = session_id_table[session_id].client_fd;

    if (pthread_mutex_unlock(&server_mutex) != 0) {
        return -1;
    }
    
    session_id_remove(session_id);
    
    if (pthread_mutex_lock(&server_mutex) != 0) {
        return -1;
    }

    active_session_counter--;
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
    server_fd = open(pipename, O_RDONLY);
    if (server_fd == -1) {
        return -1;
    }

    /* The server will run indefinitely, waiting for requests from the clients */
    while(1) {
        /* Buffer that stores request's fields (OP_CODE + rest of the fields) */
        char request_buffer[MAX_REQUEST_SIZE];

        // while para fazer read ate ler tudo, verificar se open/read retorna 0, fazer close() e open() do server.
        // while ate open funcionar, errno == EINT
        // shutdown -> exit(0)

        if (read_until_success(server_fd, request_buffer, MAX_REQUEST_SIZE) != 0) {
            close(server_fd);
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