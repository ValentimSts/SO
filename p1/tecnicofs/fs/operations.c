#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* methods that need i-node locking:
 * - data_block_get
 * - inode_get
 * - get_open_file_entry
 */


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
    size_t read_offset, write_offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */

        /* Protect the "inode_get" function call */
        if (inode_rdlock(inum) == -1) {
            return -1;
        }

        inode_t *inode = inode_get(inum);

        if (inode_unlock(inum) == -1) {
            return -1;
        }
        if (inode == NULL) {
            return -1;
        }

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            /* In thsi section we are very careful while locking the i-node,
             * to avoid nested locks, since "free_inode_blocks" already locks the i-node */
            if (inode_rdlock(inum) == -1) {
                return -1;
            }

            if (inode->i_size > 0) {
                if (inode_unlock(inum) == -1) {
                    return -1;
                }

                /* Frees all the data blocks associated with the inode */
                if(free_inode_blocks(inum) == -1) {
                    return -1;
                }

                if (inode_wrlock(inum) == -1) {
                    return -1;
                }
                inode->i_size = 0;
            }

            if (inode_unlock(inum) == -1) {
                return -1;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            if (inode_rdlock(inum) == -1) {
                return -1;
            }

            /* TODO: decide which offset to use: offset = inode->i_size */
            read_offset = inode->i_size;
            write_offset = inode->i_size;

            if (inode_unlock(inum) == -1) {
                return -1;
            }
        } else {
            /* offset = 0 */
            read_offset = 0;
            write_offset = 0;
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

        /* offset = 0 */
        read_offset = 0;
        write_offset = 0;
    } else {
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    /* return add_to_open_file_table(inum, offset); */
    return add_to_open_file_table(inum, read_offset, write_offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}


int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    /* Size of the remaining characters to write, in case the block
     * isn't big enough */
    size_t write_scraps = 0;

    /* Protect the "get_open_file_entry" function call */
    if (of_rdlock(fhandle) == -1) {
        return -1;
    }

    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        of_unlock(fhandle);
        return -1;
    }

    /* "Store" the inumber in a variable so it can be used
     * without accessing the file's struct fields */
    int inumber = file->of_inumber;
    
    if (of_unlock(fhandle) == -1) {
        return -1;
    }

    /* Protect the "inode_get" function call */
    if (inode_rdlock(inumber) == -1) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(inumber);
    if (inode == NULL) {
        inode_unlock(inumber);
        return -1;
    }

    if (inode_unlock(inumber) == -1) {
        return -1;
    }

    if (of_rdlock(fhandle) == -1) {
        return -1;
    }

    /* Finds the "true offset", since the offset is incremented the same
     * way as the inode size (when it comes to tfs_write), the actual offset we
     * want is the offset of the current block we are in, and so we find
     * that value */
    /* file->of_write_offset tinha inode->i_size */
    size_t number_of_blocks = file->of_write_offset / BLOCK_SIZE;
    size_t real_offset = file->of_write_offset - number_of_blocks * BLOCK_SIZE;

    if (of_unlock(fhandle) == -1) {
        return -1;
    }

    /* Determine how many bytes to write */
    if (to_write + real_offset > BLOCK_SIZE) {
        size_t temp = to_write;
        to_write = BLOCK_SIZE - real_offset;

        if (inode_rdlock(inumber) == -1) {
            return -1;
        }

        /* If there are still blocks available for the rest of the data we want
         * to write. If there are still indirect blocks available it means that
         * there is atleast one data block available, whether that block is a 
         * direct one or not, it doesn't matter to us now. (For some reason we have
         * to make a cast to int for the "if" to work) */
        if ((int) inode->i_curr_indir < (int) (INDIR_BLOCK_SIZE - 1)) {
            write_scraps = temp - to_write;
        }

        if (inode_unlock(inumber) == -1) {
            return -1;
        }
    }

    if (to_write > 0) {
        /* Protects the next section of code from i-node reads/writes
         * aswell as certain function calls */
        if (inode_wrlock(inumber) == -1) {
            return -1;
        }

        /* TODO: check block initialization */
        if (inode->i_size == 0) {
            /* If empty file, allocate new blocks */
            for (size_t i = 0; i < MAX_FILE_BLOCKS; i++) {
                inode->i_data_blocks[i] = data_block_alloc();

                if (inode->i_data_blocks[i] == -1) {
                    inode_unlock(inumber);
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
                    inode_unlock(inumber);
                    return -1;
                }

                inode->i_curr_indir = 0;

                /* We get the indirect block */
                int *temp = (int *)data_block_get(inode->i_indir_block);
                if (temp == NULL) {
                    inode_unlock(inumber);
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
                inode_unlock(inumber);
                return -1;
            }

            /* We need to allocate a new block for the idirect block */
            if (temp[inode->i_curr_indir] == -1) {

                temp[inode->i_curr_indir] = data_block_alloc();
                if (temp[inode->i_curr_indir] == -1) {
                    inode_unlock(inumber);
                    return -1;
                }
            }

            /* We finally get the block we need to write our data */
            block = data_block_get(temp[inode->i_curr_indir]);

            /* When write_scraps is greater than 0, it means we still have data
             * to write, in other words, we need an extra block to write the rest
             * of the data, and so, we increment i_curr_indir. If write_scraps is
             * 0 but we filled a block, then we also increment i_curr_indir, since
             * there is no more empty space in that block */
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

        if (inode_unlock(inumber) == -1) {
            return -1;
        }

        if (block == NULL) {
            return -1;
        }

        /* Perform the actual write */
        memcpy(block + real_offset, buffer, to_write);

        /* Debugging */
        /*
        char *block_content = (char *)block;
        printf("inode size: %ld\nfile offset: %ld\nwrite scraps: %ld\nreal offset: %ld\nbytes written: %ld\nblock content (write):\n%s\n----\n",
                inode->i_size, file->of_offset, write_scraps, real_offset, to_write, block_content);
        */

        if (inode_wrlock(inumber) == -1) {
            return -1;
        }

        /* The offset  and i-node size associated with the file handle are
         * incremented accordingly */
        inode->i_size += to_write;

        if (inode_unlock(inumber) == -1) {
            return -1;
        }
        
        /* Normally we would increment the file offset aswell (file->of_offset += to_write),
         * since we wrote "to_write" bytes it would only make sense to increment the offset
         * with those same bytes, yet, since for "tfs_write" our only concern with an offset
         * is the offset of the current block we're in (real_offset), that we can find using
         * only the inode size, we choose not to increment file->offset in return for a more 
         * viable offset option that makes a "tfs_read" after a "tfs_write" (vice-versa)
         * possible aswell as offerring the possibility of simultaneous read and write
         * operations to the same file. */
        
        /* TODO: decide which offset to use: file->of_offset += to_write; */
        if (of_wrlock(fhandle) == -1) {
            return -1;
        }

        file->of_write_offset += to_write;

        if (of_unlock(fhandle) == -1) {
            return -1;
        }

        /* If write_scraps is greater than 0 it means we still have data to
         * write, and so we do a recursive call to finish writing the remaining
         * data */
        if (write_scraps > 0) {
            tfs_write(fhandle, buffer + to_write, write_scraps);
        }
    }

    /* We return the true ammount of data we wrote to the file */
    to_write += write_scraps;

    return (ssize_t)to_write;
}


ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    /* Protect the "get_open_file_entry" funtion call */
    if (of_rdlock(fhandle) == -1) {
        return -1;
    }

    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* Just like we did in "tfs_write", we do the same here */
    int inumber = file->of_inumber;

    if (of_unlock(fhandle) == -1) {
        return -1;
    }

    /* Protect the "inode_get" function call */
    if (inode_rdlock(inumber) == -1) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        inode_unlock(inumber);
        return -1;
    }

    if (of_rdlock(fhandle) == -1) {
        return -1;
    }

    /* Determine how many bytes to read */
    size_t to_read = inode->i_size - file->of_read_offset;
    
    if (of_unlock(fhandle) == -1) {
        return -1;
    }

    if (inode_unlock(inumber) == -1) {
        return -1;
    }

    /* TODO: check if we need recursiveness with the "to_read" variable,
     *       like with "to_write" on "tfs_write" */
    if (to_read > len) {
        to_read = len;
    }

    if (to_read > 0) {
        if (of_rdlock(fhandle) == -1) {
            return -1;
        }

        /* Finds the block where the offset is, aswell as the "actual"
         * offset for that same block. (similar to the tfs_write "real_offset"
         * logic) */
        size_t offset_block = file->of_read_offset / BLOCK_SIZE;
        size_t real_offset = file->of_read_offset - offset_block * BLOCK_SIZE;

        if (of_unlock(fhandle) == -1) {
            return -1;
        }

        void *block = NULL;

        /* Protect the "data_block_get" function calls aswell as i-node reads */
        if (inode_rdlock(inumber) == -1) {
            return -1;
        }

        /* In case the offset block is one of the direct blocks */
        if (offset_block < MAX_FILE_BLOCKS) {
            block = data_block_get(inode->i_data_blocks[offset_block]);
        }
        else {
            /* The offset block is an indirect one */
            int *temp = (int *)data_block_get(inode->i_indir_block);
            if (temp == NULL) {
                inode_unlock(inumber);
                return -1;
            }

            block = data_block_get(temp[offset_block - MAX_FILE_BLOCKS]);
        }

        if (inode_unlock(inumber) == -1) {
            return -1;
        }

        if (block == NULL) {
            return -1;
        }

        /* Perform the actual read */
        memcpy(buffer, block + real_offset, to_read);
    
        /* Debugging */
        /*
        char *buffer_content = (char *)buffer;
        char *block_content = (char *)block;
        printf("file offset: %ld\nreal offset: %ld\ndata read:\n%s\n-----\n",file->of_offset, real_offset, buffer_content);
        printf("block content:\n%s\n----\n", block_content);
        */

        if (of_wrlock(fhandle) == -1) {
            return -1;
        }

        /* The offset associated with the file handle is
         * incremented accordingly */
        file->of_read_offset += to_read;

        if (of_unlock(fhandle) == -1) {
            return -1;
        }
    }

    return (ssize_t)to_read;
}


int tfs_copy_to_external_fs(char const *source_path, char const *dest_path) {
    int fd = tfs_open(source_path, 0);
    if (fd == -1) {
        return -1;
    }

    int inum = tfs_lookup(source_path);
    if (inum == -1) {
        return -1;
    }

    /* Protect the "inode_get" function call */
    if (inode_rdlock(inum) == -1) {
        return -1;
    } 

    inode_t *inode = inode_get(inum);
    if (inode == NULL) {
        inode_unlock(inum);
        return -1;
    }

    size_t size = inode->i_size;

    if (inode_unlock(inum) == -1) {
        return -1;
    }

    /* Declare a buffer with enough space for all the file's content */
    char buffer[size];

    /* Read the file's content and stores it in the buffer */
    if (tfs_read(fd, buffer, size) != size) {
        return -1;
    }

    /* Opens the file given as the des_path in write mode, if that file
     * does not exist then it is created, if it does, all its content is
     * erased and the file is considered as a new empty file (functionality
     * already implemented by the fopen() function)
     * - See: https://www.ibm.com/docs/en/i/7.1?topic=functions-fopen-open-files */
    FILE *fp = fopen(dest_path, "w");
    if (fp == NULL) {
        return -1;
    }

    /* Perform the actua write to the file */
    if (fwrite(buffer, 1, size, fp) != size) {
        return -1;
    }
    
    /* Close the external FS file */
    if (fclose(fp) == EOF) {
        return -1;
    }

    /* Close the TecnicoFS file */
    if (tfs_close(fd) == -1) {
        return -1;
    }

    return 0;
}