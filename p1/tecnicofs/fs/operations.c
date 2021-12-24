#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tfs_init() {
    state_init();

    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}


int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
            return -1;
        }

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                if (inode_data_block_free(inum) == -1) {
                    return -1;
                }
                inode->i_size = 0;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {

            /* If the file has multiple blocks associated with it, we 
             * determine the correct offset (end of the last block used) */
            if (inode->i_size > BLOCK_SIZE) {
                size_t number_of_blocks = inode->i_size / BLOCK_SIZE;
                offset = inode->i_size - BLOCK_SIZE*number_of_blocks;
            }
            else {
                offset = inode->i_size;
            }
        } else {
            offset = 0;
        }
    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created*/
        /* Create inode */
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else {
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    return add_to_open_file_table(inum, offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}


int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    /* Size of the remaining characters to wtite, in case the block
       isn't big enough */
    size_t write_scraps = 0;

    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to write */
    if (to_write + file->of_offset > BLOCK_SIZE) {
        size_t temp = to_write;
        to_write = BLOCK_SIZE - file->of_offset;

        /* If there are still blocks available for the rest of the
         * string we want to erite */
        if (inode->i_curr_indir != INDIRECT_BLOCK_SIZE-1) {
            write_scraps = temp - to_write;
        }
    }

    if (to_write > 0) {

        if (inode->i_size == 0) {
            /* If empty file, allocate new block */
            *inode->i_data_blocks[inode->i_curr_block] = data_block_alloc();
        }

        void *block = NULL;

        /* We are still in the direct blocks */
        if (inode->i_curr_block < MAX_FILE_BLOCKS) {
            block = data_block_get(*inode->i_data_blocks[inode->i_curr_block++]);
        }
        else {
            /* We're already using the indirect block.
             * If "i_cur_indir" == INDIRECT_BLOCK_SIZE, then there is no more
             * memory left for that file, and so we return-1 (error) */
            if (inode->i_curr_indir == INDIRECT_BLOCK_SIZE)
                return -1;
            block = data_block_get(*inode->i_data_blocks[inode->i_curr_indir++]);
        }

        if (block == NULL) {
            return -1;
        }

        /* Perform the actual write */
        memcpy(block + file->of_offset, buffer, to_write);

        /* The offset associated with the file handle is
         * incremented accordingly */
        if (write_scraps > 0) {
            /* TODO: review later. Understand how offset works!!*/
            
            /* We have already filled a block so the size is incremented accordingly */
            inode->i_size += BLOCK_SIZE;

            /* Resets the offset since we are now in a new block */
            file->of_offset = write_scraps;
            tfs_write(fhandle, buffer + to_write, write_scraps);
        }
        else {
            file->of_offset += to_write;
        }

        inode->i_size += file->of_offset;
    }

    if (write_scraps > 0) {
        to_write += write_scraps;
    }

    return (ssize_t)to_write;
}


ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    // TODO: change tfs_read to work with multiple block files

    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to read */
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    if (to_read > 0) {
        int block_index;

        /* TODO: add comments */

        if (inode->i_curr_block < MAX_FILE_BLOCKS) {
            block_index = inode->i_curr_block;
        }
        else {
            if (inode->i_curr_indir == INDIRECT_BLOCK_SIZE)
                return -1;
            block_index = inode->i_curr_indir;
        }

        void *block = data_block_get(block_index);
        if (block == NULL) {
            return -1;
        }

        /* Perform the actual read */
        memcpy(buffer, block + file->of_offset, to_read);
        /* The offset associated with the file handle is
         * incremented accordingly */
        file->of_offset += to_read;
    }

    return (ssize_t)to_read;
}
