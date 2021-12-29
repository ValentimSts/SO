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
                /* Frees all the data blocks associated with the inode */
                if(free_inode_blocks(inum) == -1) {
                    return -1;
                }
                inode->i_size = 0;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;

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
     * isn't big enough */
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

    /* Finds the "true offset", since the offset is incremented the same
     * way as the inode size (when it comes to tfs_write), the actual offset we
     * want is the offset of the current block we are in, and so we find
     * that value */
    size_t number_of_blocks = inode->i_size / BLOCK_SIZE;
    size_t real_offset = inode->i_size - number_of_blocks * BLOCK_SIZE;

    /* Debugging */
    printf("to write: %ld\n", to_write);

    /* Determine how many bytes to write */
    if (to_write + real_offset > BLOCK_SIZE) {

        printf("joao1\n");

        size_t temp = to_write;
        to_write = BLOCK_SIZE - file->of_offset;

        /* If there are still blocks available for the rest of the data we want
         * to write. If there are still indirect blocks available it means that
         * there is atleast one data block available, whether that block is a 
         * direct one or not, it doesn't matter to us now.  */

        printf("curr indir: %d\nindir size: %ld\n", inode->i_curr_indir, INDIR_BLOCK_SIZE);
        printf("if value: %d\n", inode->i_curr_indir < (INDIR_BLOCK_SIZE));

        if (inode->i_curr_indir < (INDIR_BLOCK_SIZE - 1)) {
            printf("joao2\n");
            write_scraps = temp - to_write;
        }
    }

    if (to_write > 0) {
        if (inode->i_size == 0) {
            /* If empty file, allocate new blocks */
            for (size_t i = 0; i < MAX_FILE_BLOCKS; i++) {
                inode->i_data_blocks[i] = data_block_alloc();

                if (inode->i_data_blocks[i] == -1) {
                    return -1;
                }
            }

            /* After allocating all the blocks we must start writing stuff
             * on the first one, and so, i_curr_block is set to 0 */
            inode->i_curr_block = 0;
        }

        void *block = NULL;

        /* If there are no more direct referenced blocks available, we must
         * use the indirect one */
        if (inode->i_curr_block == MAX_FILE_BLOCKS) {

            /* If the inode doesn't have an indirect block, we initialize it */
            if (inode->i_indir_block == -1) {
                /* Allocates a new indirect data block */
                inode->i_indir_block = data_block_alloc();
                if (inode->i_indir_block == -1) {
                    return -1;
                }

                inode->i_curr_indir = 0;

                /* We get the indirect block */
                int *temp = (int *)data_block_get(inode->i_indir_block);
                if (temp == NULL) {
                    return -1;
                }

                /* Initialize the indirect block's content as -1, meaning
                 * it is currently empty */
                for (size_t i = 0; i < INDIR_BLOCK_SIZE; i++) {
                    temp[i] = -1;
                } 
            }

            /* We get the indirect block */
            int *temp = (int *)data_block_get(inode->i_indir_block);
            if (temp == NULL) {
                return -1;
            }

            /* We need to allocate a new block for the idirect block */
            if (temp[inode->i_curr_indir] == -1) {

                temp[inode->i_curr_indir] = data_block_alloc();
                if (temp[inode->i_curr_indir] == -1) {
                    return -1;
                }
            }

            /* We finally get the block we need to write our data */
            block = data_block_get(temp[inode->i_curr_indir]);

            /* When write_scraps is greater than 0, it means we still have data
             * to write, in other words, we need an extra block to write the rest
             * of the data, and so, we increment i_curr_indir */
            if (write_scraps > 0 || to_write + real_offset == BLOCK_SIZE) {
                inode->i_curr_indir++;
            }
        }
        else {  
            block = data_block_get(inode->i_data_blocks[inode->i_curr_block]);

            /* Just like we did with the indirect block curr variable, we do the
             * same with the direct block curr variable */
            if (write_scraps > 0 || to_write + real_offset == BLOCK_SIZE) {
                inode->i_curr_block++;
            }
        }

        if (block == NULL) {
            return -1;
        }

        /* Perform the actual write */
        memcpy(block + real_offset, buffer, to_write);

        /* Debugging */
        
        char *block_content = (char *)block;
        printf("inode size: %ld\nfile offset: %ld\nwrite scraps: %ld\nreal offset: %ld\nbytes written: %ld\nblock content (write):\n%s\n----\n",
                inode->i_size, file->of_offset, write_scraps, real_offset, to_write, block_content);
        

        /* The offset associated with the file handle is
         * incremented accordingly */
        if (write_scraps > 0) {            
            /* We have already filled a block so the size is incremented accordingly */

            /* TODO: review this */
            inode->i_size += to_write;

            /* Finds the right offset after the write */
            file->of_offset = inode->i_size;

            tfs_write(fhandle, buffer + to_write, write_scraps);
        }
        else {
            file->of_offset += to_write;
        }

        inode->i_size += to_write;
    }

    /* We return the true ammount of data we wrote to the file */
    to_write += write_scraps;

    return (ssize_t)to_write;
}


ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
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
        
        /* Finds the block where the offset is, aswell as the "actual"
         * offset for that same block. (similar to the tfs_write "real_offset"
         * logic) */
        size_t offset_block = file->of_offset / BLOCK_SIZE;
        size_t real_offset = file->of_offset - offset_block * BLOCK_SIZE;

        void *block = NULL;

        /* In case the offset block is one of the direct blocks */
        if (offset_block < MAX_FILE_BLOCKS) {
            block = data_block_get(inode->i_data_blocks[offset_block]);
        }
        /* The offset block is an indirect one */
        else {
            int *temp = (int *)data_block_get(inode->i_indir_block);
            block = data_block_get(temp[offset_block - MAX_FILE_BLOCKS]);
        }

        if (block == NULL) {
            return -1;
        }

        /* Perform the actual read */
        memcpy(buffer, block + real_offset, to_read);
        /* The offset associated with the file handle is
         * incremented accordingly */

        /* Debugging */
        /*
        char *buffer_content = (char *)buffer;
        char *block_content = (char *)block;
        printf("file offset: %ld\nreal offset: %ld\ndata read:\n%s\n-----\n",file->of_offset, real_offset, buffer_content);
        printf("block content:\n%s\n----\n", block_content);
        */

        file->of_offset += to_read;
    }

    return (ssize_t)to_read;
}

