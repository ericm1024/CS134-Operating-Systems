// lab3a.c
//
// NAME: Eric Mueller
// EMAIL: emueller@hmc.edu
// ID: 40160869

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ext2_fs.h"

// exit, possibly with a message
static void die(const char *reason, int err)
{
        if (reason) {
                fprintf(stderr, "%s: %s\n", reason, strerror(err));
                exit(1);
        } else {
                exit(0);
        }
}

static uint8_t *map_file(const char *fname)
{
        int fd = open(fname, O_RDONLY);
        if (fd < 0)
                die("open", errno);

        struct stat st;
        int err = fstat(fd, &st);
        if (err)
                die("fstat", errno);
        
        uint8_t *img = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (img == MAP_FAILED)
                die("mmap", errno);

        return img;
}

int main(int argc, char **argv)
{
        if (argc != 2)
                die("expected filename", EINVAL);

        uint8_t *img = map_file(argv[1]);
        
        struct ext2_super_block *sb = (void*)(img + 1024);
        if (sb->s_magic != EXT2_SUPER_MAGIC)
                die("superblock magic was wrong", EIO);
        
        size_t bsize = EXT2_MIN_BLOCK_SIZE << sb->s_log_block_size;

        // print superblock stats
        printf("SUPERBLOCK,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
               (size_t)sb->s_blocks_count,
               (size_t)sb->s_inodes_count,
               (size_t)bsize,
               (size_t)sb->s_inode_size,
               (size_t)sb->s_blocks_per_group,
               (size_t)sb->s_inodes_per_group,
               (size_t)sb->s_first_ino);

        // print block group summaries
        size_t nr_blk_grps = sb->s_blocks_count/sb->s_blocks_per_group +
                (sb->s_blocks_count%sb->s_blocks_per_group > 0 ? 1 : 0);

        struct ext2_group_desc *grps = (void*)(img + bsize *
                                               (sb->s_first_data_block + 1));

        // the last block group might have a non-standard number of blocks
        // if the fs size does not evenly divide the block group 
        size_t blocks_in_last_group = sb->s_blocks_per_group;
        // XXX: what about the first block group?
        if (sb->s_blocks_count%sb->s_blocks_per_group != 0)
                blocks_in_last_group = sb->s_blocks_count
                        %sb->s_blocks_per_group;
        
        for (size_t i = 0; i < nr_blk_grps; ++i) {
                struct ext2_group_desc *bg = grps + i;
                printf("GROUP,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
                       (size_t)i,
                       (size_t)(i == nr_blk_grps - 1 ? blocks_in_last_group
                                : sb->s_blocks_per_group),
                       // XXX: does the last group necessarily have
                       // the same number of inodes as all the others?
                       (size_t)sb->s_inodes_per_group,
                       (size_t)bg->bg_free_blocks_count,
                       (size_t)bg->bg_free_inodes_count,
                       (size_t)bg->bg_block_bitmap,
                       (size_t)bg->bg_inode_bitmap,
                       (size_t)bg->bg_inode_table);
        }

        // print free block summaries
        for (size_t i = 0; i < nr_blk_grps; ++i) {
                struct ext2_group_desc *bg = grps + i;
                uint8_t *bmap = img + bg->bg_block_bitmap*bsize;
                size_t blocks = i == nr_blk_grps - 1 ? blocks_in_last_group
                        : sb->s_blocks_per_group;

                printf("blocks=%lu\n", blocks);
                
                for (size_t j = 0; j < blocks; ++j) {
                        // if the bit is 0, the block is free
                        if ((bmap[j/8] & (1 << (j%8))) == 0)
                                printf("BFREE,%lu\n",
                                       i*sb->s_blocks_per_group + j);
                }
        }
}
