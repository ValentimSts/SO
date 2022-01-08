#include "state.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>


/* Persistent FS state (in reality, it should be maintained in secondary
 * memory; for simplicity, this project maintains it in primary memory) */

/* rwlock for protecting the state structures */
static pthread_rwlock_t fs_rwlock;

/* I-node table */
static inode_t inode_table[INODE_TABLE_SIZE];
static char freeinode_ts[INODE_TABLE_SIZE];

/* Data blocks */
static char fs_data[BLOCK_SIZE * DATA_BLOCKS];
static char free_blocks[DATA_BLOCKS];

/* TODO: review this */
/* static pthread_rwlock_t block_locks[DATA_BLOCKS]; */


/* Volatile FS state */

static open_file_entry_t open_file_table[MAX_OPEN_FILES];
static char free_open_file_entries[MAX_OPEN_FILES];
 
static inline bool valid_inumber(int inumber) {
    return inumber >= 0 && inumber < INODE_TABLE_SIZE;
}

static inline bool valid_block_number(int block_number) {
    return block_number >= 0 && block_number < DATA_BLOCKS;
}

static inline bool valid_file_handle(int file_handle) {
    return file_handle >= 0 && file_handle < MAX_OPEN_FILES;
}

/**
 * We need to defeat the optimizer for the insert_delay() function.
 * Under optimization, the empty loop would be completely optimized away.
 * This function tells the compiler that the assembly code being run (which is
 * none) might potentially change *all memory in the process*.
 *
 * This prevents the optimizer from optimizing this code away, because it does
 * not know what it does and it may have side effects.
 *
 * Reference with more information: https://youtu.be/nXaxk27zwlk?t=2775
 *
 * Exercise: try removing this function and look at the assembly generated to
 * compare.
 */
static void touch_all_memory() { __asm volatile("" : : : "memory"); }

/*
 * Auxiliary function to insert a delay.
 * Used in accesses to persistent FS state as a way of emulating access
 * latencies as if such data structures were really stored in secondary memory.
 */
static void insert_delay() {
    for (int i = 0; i < DELAY; i++) {
        touch_all_memory();
    }
}

/*
 * Initializes FS state
 */
void state_init() {
    for (size_t i = 0; i < INODE_TABLE_SIZE; i++) {
        freeinode_ts[i] = FREE;
    }

    for (size_t i = 0; i < DATA_BLOCKS; i++) {
        free_blocks[i] = FREE;
    }

    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        free_open_file_entries[i] = FREE;
    }

     /* Initializes the rwlock for later use */
    if (pthread_rwlock_init(&fs_rwlock, NULL) != 0) { 
        printf("state_init(): lock initialization failed\n");
        return;
    }
}

void state_destroy() {
    /* nothing to do */
}

/*
 * Creates a new i-node in the i-node table.
 * Input:
 *  - n_type: the type of the node (file or directory)
 * Returns:
 *  new i-node's number if successfully created, -1 otherwise
 */
int inode_create(inode_type n_type) {
    if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
        return -1;
    }

    for (int inumber = 0; inumber < INODE_TABLE_SIZE; inumber++) {
        if ((inumber * (int) sizeof(allocation_state_t) % BLOCK_SIZE) == 0) {
            insert_delay(); // simulate storage access delay (to freeinode_ts)
        }

        /* Finds first free entry in i-node table */
        if (freeinode_ts[inumber] == FREE) {
            /* Found a free entry, so takes it for the new i-node*/
            freeinode_ts[inumber] = TAKEN;
            insert_delay(); // simulate storage access delay (to i-node)
            inode_table[inumber].i_node_type = n_type;

            if (n_type == T_DIRECTORY) {
                /* We unlock the rwlock so that we don't have nested wrlock's 
                 * when calling "data_block_alloc()" */
                if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                    return -1;
                }

                /* Initializes directory (filling its block with empty
                 * entries, labeled with inumber==-1) */
                int b = data_block_alloc();
                if (b == -1) {
                    freeinode_ts[inumber] = FREE;
                    return -1;
                }

                /* We "re-lock" the rwlock that we unlocked previously 
                 * since we now need it */
                if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
                    return -1;
                }

                /* Initializes the i-node's rwlock, for future usage */
                if (pthread_rwlock_init(&inode_table[inumber].i_lock, NULL) != 0) {
                    return -1;
                }

                inode_table[inumber].i_size = BLOCK_SIZE;

                /* Since the i-node is a directory we only use one of the data blocks,
                 * the first one. */
                inode_table[inumber].i_data_blocks[0] = b;
                inode_table[inumber].i_curr_block = 0;

                /* The remaining data blocks are initialized with -1, since they won't
                 * be used */
                for (size_t i = 1; i < MAX_FILE_BLOCKS; i++) {
                    inode_table[inumber].i_data_blocks[i] = -1;
                }
                /* Likewise, the indirect block is also set to -1, as it won't be used */
                inode_table[inumber].i_indir_block = -1;
                inode_table[inumber].i_curr_indir = -1;

                dir_entry_t *dir_entry = (dir_entry_t *)data_block_get(b);
                if (dir_entry == NULL) {
                    freeinode_ts[inumber] = FREE;

                    if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                        return -1;
                    }
                    return -1;
                }

                for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
                    dir_entry[i].d_inumber = -1;
                }
            } else {
                /* Initializes the i-node's rwlock, for future locking */
                if (pthread_rwlock_init(&inode_table[inumber].i_lock, NULL) != 0) {
                    return -1;
                }

                /* In case of a new file, simply sets its size to 0 */
                inode_table[inumber].i_size = 0;
                
                /* We initialize every data block entry with a -1, meaning they
                 * are empty */
                for (size_t i = 0; i < MAX_FILE_BLOCKS; i++) {
                    inode_table[inumber].i_data_blocks[i] = -1;
                }
                /* the current data block is also initialized at -1, as we haven't
                 * actually used the data blocks */
                inode_table[inumber].i_curr_block = -1;

                /* the indirect block is also set to -1 */
                inode_table[inumber].i_indir_block = -1;
                inode_table[inumber].i_curr_indir = -1;                
            }

            if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                return -1;
            }

            return inumber;
        }
    }

    if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

    return -1;
}

/*
 * Deletes the i-node.
 * Input:
 *  - inumber: i-node's number
 * Returns: 0 if successful, -1 if failed
 */
int inode_delete(int inumber) {
    // simulate storage access delay (to i-node and freeinode_ts)
    insert_delay();
    insert_delay();

    if (!valid_inumber(inumber) || freeinode_ts[inumber] == FREE) {
        return -1;
    }

    if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
        return -1;
    }

    freeinode_ts[inumber] = FREE;

    if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

    if (inode_table[inumber].i_size > 0) {
        /* Free all the data blocks associated with the inode */
        if(free_inode_blocks(inumber) == -1) {
            return -1;
        }
    }

    return 0;
}

/*
 * Returns a pointer to an existing i-node.
 * Input:
 *  - inumber: identifier of the i-node
 * Returns: pointer if successful, NULL if failed
 */
inode_t *inode_get(int inumber) {
    if (!valid_inumber(inumber)) {
        return NULL;
    }

    insert_delay(); // simulate storage access delay to i-node
    return &inode_table[inumber];
}

/*
 * Adds an entry to the i-node directory data.
 * Input:
 *  - inumber: identifier of the i-node
 *  - sub_inumber: identifier of the sub i-node entry
 *  - sub_name: name of the sub i-node entry
 * Returns: SUCCESS or FAIL
 */
int add_dir_entry(int inumber, int sub_inumber, char const *sub_name) {
    if (!valid_inumber(inumber) || !valid_inumber(sub_inumber)) {
        return -1;
    }

    insert_delay(); // simulate storage access delay to i-node with inumber
    if (inode_table[inumber].i_node_type != T_DIRECTORY) {
        return -1;
    }

    if (strlen(sub_name) == 0) {
        return -1;
    }

	if (pthread_rwlock_rdlock(&fs_rwlock) != 0) {
        return -1;
    }

    /* Locates the block containing the directory's entries. Index 0, since
     * we are dealing with a directory */
    dir_entry_t *dir_entry =
        (dir_entry_t *)data_block_get(inode_table[inumber].i_data_blocks[0]);
    
    if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

    if (dir_entry == NULL) {
        return -1;
    }

	if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
        return -1;
    }

    /* Finds and fills the first empty entry */
    for (size_t i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (dir_entry[i].d_inumber == -1) {
            dir_entry[i].d_inumber = sub_inumber;
            strncpy(dir_entry[i].d_name, sub_name, MAX_FILE_NAME - 1);
            dir_entry[i].d_name[MAX_FILE_NAME - 1] = 0;

            if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                return -1;
            }

            return 0;
        }
	}

    if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

    return -1;
}

/* Looks for a given name inside a directory
 * Input:
 * 	- parent directory's i-node number
 * 	- name to search
 * 	Returns i-number linked to the target name, -1 if not found
 */
int find_in_dir(int inumber, char const *sub_name) {
    insert_delay(); // simulate storage access delay to i-node with inumber
    if (!valid_inumber(inumber) ||
        inode_table[inumber].i_node_type != T_DIRECTORY) {
        return -1;
    }

	if (pthread_rwlock_rdlock(&fs_rwlock) != 0) {
        return -1;
    }

    /* Locates the block containing the directory's entries. Index 0, since
     * we are dealing with a directory */
    dir_entry_t *dir_entry =
        (dir_entry_t *)data_block_get(inode_table[inumber].i_data_blocks[0]);
    if (dir_entry == NULL) {
        return -1;
    }
    
	if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

	if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
        return -1;
    }

    /* Iterates over the directory entries looking for one that
     * has the target name */
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if ((dir_entry[i].d_inumber != -1) &&
            (strncmp(dir_entry[i].d_name, sub_name, MAX_FILE_NAME) == 0)) {
            if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                return -1;
            }

            return dir_entry[i].d_inumber;
        }
    }
    
	if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

    return -1;
}

/*
 * Allocated a new data block
 * Returns: block index if successful, -1 otherwise
 */
int data_block_alloc() {
	if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
        return -1;
    }

    for (int i = 0; i < DATA_BLOCKS; i++) {
        if (i * (int) sizeof(allocation_state_t) % BLOCK_SIZE == 0) {
            insert_delay(); // simulate storage access delay to free_blocks
        }
        if (free_blocks[i] == FREE) {
            free_blocks[i] = TAKEN;
            if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                return -1;
            }

            return i;
        }
    }
    
	if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

    return -1;
}

/* Frees a data block
 * Input
 * 	- the block index
 * Returns: 0 if success, -1 otherwise
 */
int data_block_free(int block_number) {
    if (!valid_block_number(block_number)) {
        return -1;
    }

    insert_delay(); // simulate storage access delay to free_blocks

    free_blocks[block_number] = FREE;
	return 0;
}

/* Returns a pointer to the contents of a given block
 * Input:
 * 	- Block's index
 * Returns: pointer to the first byte of the block, NULL otherwise
 */
void *data_block_get(int block_number) {
    if (!valid_block_number(block_number)) {
        return NULL;
    }

    insert_delay(); // simulate storage access delay to block
    return &fs_data[block_number * BLOCK_SIZE];
}

/* Add new entry to the open file table
 * Inputs:
 * 	- I-node number of the file to open
 * 	- Initial offset
 * Returns: file handle if successful, -1 otherwise
 */
int add_to_open_file_table(int inumber, size_t offset) {
	if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
        return -1;
    }

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (free_open_file_entries[i] == FREE) {
            free_open_file_entries[i] = TAKEN;

            /* Initializes the rwlock for the open file for future usage */
            if (pthread_rwlock_init(&open_file_table[i].of_lock, NULL) != 0) {
                return -1;
            }

            open_file_table[i].of_inumber = inumber;
            open_file_table[i].of_offset = offset;

	        if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                return -1;
            }

            return i;
        }
    }

	if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

    return -1;
}

/* Frees an entry from the open file table
 * Inputs:
 * 	- file handle to free/close
 * Returns 0 is success, -1 otherwise
 */
int remove_from_open_file_table(int fhandle) {
    if (!valid_file_handle(fhandle) ||
        free_open_file_entries[fhandle] != TAKEN) {
        return -1;
    }

	if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
        return -1;
    }

    free_open_file_entries[fhandle] = FREE;

    /* Since the file will be closed, we destroy the rwlock associated
     * with it */
    if (pthread_rwlock_destroy(&open_file_table[fhandle].of_lock) != 0) {
       return -1;
    }

	if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

    return 0;
}

/* Returns pointer to a given entry in the open file table
 * Inputs:
 * 	 - file handle
 * Returns: pointer to the entry if sucessful, NULL otherwise
 */
open_file_entry_t *get_open_file_entry(int fhandle) {
    if (!valid_file_handle(fhandle)) {
        return NULL;
    }

    return &open_file_table[fhandle];
}

/* Frees all the data blocks associated with an i-node
 * Inputs:
 *   - inumber: i-node's number
 * Returns: 0 if successful -1 otherwise 
 */
int free_inode_blocks(int inumber) {
    if (!valid_inumber(inumber)) {
        return -1;
    }

    /* We apply a wrlock to our rwlock to protect the 
     * "data_block_free(...)" call, since this method "isn't
     * thread-safe". */
    if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
        return -1;
    }

    /* Free all the direct referenced data blocks associated with
     * the inode */
    for(size_t i = 0; i < MAX_FILE_BLOCKS; i++) {
        if (data_block_free(inode_table[inumber].i_data_blocks[i]) == -1) {
            if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                return -1;
            }
            return -1;
        }
    }

    if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
        return -1;
    }

    /* If we have an indirect block allocated we free it */
    if (inode_table[inumber].i_indir_block != -1) {
		if (pthread_rwlock_rdlock(&fs_rwlock) != 0) {
            return -1;
        }
		
        /* We get the indirect block */
        int *block = (int *)data_block_get(inode_table[inumber].i_indir_block);
        if (block == NULL) {
            if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                return -1;
            }
            return -1;
        }

		if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
            return -1;
        }
		
        if (pthread_rwlock_wrlock(&fs_rwlock) != 0) {
            return -1;
        }

        /* Free all the blocks in the indirect block */
        for (size_t i = 0; i < inode_table[inumber].i_curr_indir; i++) {
            if (data_block_free(block[i]) == -1) {
                if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                    return -1;
                }
                return -1;
            }
        }

        /* Free the data block itself */
        if (data_block_free(inode_table[inumber].i_indir_block) == -1) {
            if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
                return -1;
            }
            return -1;
        }
        
        if (pthread_rwlock_unlock(&fs_rwlock) != 0) {
            return -1;
        }
    }

    return 0;
}

/* Applies a read-lock to the i-node's rwlock
 * Inputs:
 *   - inumber: i-node's number
 * Returns: 0 if successful -1 otherwise 
 */
int inode_rdlock(int inumber) {
    if (!valid_inumber(inumber)) {
        return -1;
    }

    if (pthread_rwlock_rdlock(&inode_table[inumber].i_lock) != 0) {
        return -1;
    }

    return 0;
}

/* Applies a write-lock to the i-node's rwlock
 * Inputs:
 *   - inumber: i-node's number
 * Returns: 0 if successful -1 otherwise 
 */
int inode_wrlock(int inumber) {
    if (!valid_inumber(inumber)) {
        return -1;
    }

    if (pthread_rwlock_wrlock(&inode_table[inumber].i_lock) != 0) {
        return -1;
    }

    return 0;
}

/* Unlocks the i-node's rwlock
 * Inputs:
 *   - inumber: i-node's number
 * Returns: 0 if successful -1 otherwise 
 */
int inode_unlock(int inumber) {
    if (!valid_inumber(inumber)) {
        return -1;
    }

    if (pthread_rwlock_unlock(&inode_table[inumber].i_lock) != 0) {
        return -1;
    }

    return 0;
}

/* Applies a read-lock to the file's rwlock
 * Returns: 0 if successful -1 otherwise 
 */
int of_rdlock(int fhandle) {
    if (!valid_file_handle(fhandle)) {
        return -1;
    }

    if (pthread_rwlock_rdlock(&open_file_table[fhandle].of_lock) != 0) {
        return -1;
    }

    return 0;
}

/* Applies a write-lock to the file's rwlock
 * Returns: 0 if successful -1 otherwise 
 */
int of_wrlock(int fhandle) {
    if (!valid_file_handle(fhandle)) {
        return -1;
    }

    if (pthread_rwlock_wrlock(&open_file_table[fhandle].of_lock) != 0) {
        return -1;
    }

    return 0;
}

/* Unlocks the file's rwlock
 * Returns: 0 if successful -1 otherwise 
 */
int of_unlock(int fhandle) {
    if (!valid_file_handle(fhandle)) {
        return -1;
    }

    if (pthread_rwlock_unlock(&open_file_table[fhandle].of_lock) != 0) {
        return -1;
    }

    return 0;
}
