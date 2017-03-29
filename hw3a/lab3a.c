// lab3a.c
//
// NAME: Eric Mueller
// EMAIL: emueller@hmc.edu
// ID: 40160869

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ext2_fs.h"

#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFLNK 0xA000
#define EXT2_S_IFDIR 0x4000
#define EXT2_MODE_FORMAT_MASK 0xf000

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

// mmaping is easier than pread(2)'ing; we can just let demand-paging do our
// work for us.
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

static void summarize_one_dentry(struct ext2_dir_entry *dentry,
                                 size_t inode_nr, size_t offset)
{
        // copy the name to a buffer first to ensure its null terminated
        // for easyier printf'ing
        char buf[EXT2_NAME_LEN + 1];
        memset(buf, 0, sizeof buf);
        memcpy(buf, dentry->name, dentry->name_len);

        if (dentry->inode != 0) {
                printf("DIRENT,%lu,%lu,%u,%u,%u,'%s'\n",
                       inode_nr,
                       offset,
                       dentry->inode,
                       (unsigned)dentry->rec_len,
                       (unsigned)dentry->name_len,
                       buf);
        }
}
// traversing indirect block trees is disgusting
static void summarize_directory(struct ext2_super_block *sb,
                                struct ext2_inode *in, size_t inode_nr,
                                uint8_t *img)
{
        size_t offset = 0;
        size_t size = in->i_size;
        struct ext2_dir_entry *dentry;
        size_t bsize = EXT2_MIN_BLOCK_SIZE << sb->s_log_block_size;
        size_t ppb = bsize/sizeof(__u32);

        // how much space is represented with direct blocks, indirect
        // blocks, doubly indirect blocks, and triply indirect blocks?
        const size_t size_in_dblks = EXT2_NDIR_BLOCKS * bsize;
        const size_t size_in_iblks = ppb * bsize + size_in_dblks;
        const size_t size_in_diblks = ppb * ppb * bsize
                + size_in_iblks;
        const size_t size_in_tiblks = ppb * ppb
                * ppb * bsize + size_in_diblks;

        uint8_t *block;

        block = (void*)(img + bsize * in->i_block[0]);

        while (offset < size) {
                //printf("parent=%lu, offset=%lu, size=%lu\n", inode_nr,
                //       offset, size);
                
                size_t blk_offset = offset % bsize;
                
                dentry = (void*)(block + blk_offset);
                summarize_one_dentry(dentry, inode_nr, offset);

                size_t rec_len = dentry->rec_len;
                offset += rec_len;

                // are we at the end of the directory?
                if (offset >= size)
                        break;

                // is the next dentry within this block? If so, keep going
                if (blk_offset + rec_len < bsize)
                        continue;

                // okay, we need to find the next block. We're going to find
                // out if it's represented directly by a block pointer
                // in the inode, indirectly, doubly indirectly, or triply
                // indirectly, and we'll handle each case separately.
                // 
                // Hold on to your briches, this code is complicated and
                // repetetive, but I think this is the clearest way to
                // write it.
                //
                // Each block uses a "this_offset" variable, which is how
                // far we are into the part of the file mapped with that
                // block's method. For example, for double indirect blocks,
                // "this_offset" is how the offset is into the part of
                // the file mapped by the inode's doubly indirect block
                
                // the next block is indexed directly?
                if (offset < size_in_dblks) {
                        block = img + bsize * in->i_block[offset/bsize];

                // is it indexed indirectly?
                } else if (offset < size_in_iblks) {
                        size_t this_offset = offset - size_in_dblks;
                        
                        __u32 *iblock = (void*)(img + bsize
                                    * in->i_block[EXT2_IND_BLOCK]);

                        __u32 bptr = iblock[this_offset/bsize];

                        block = img + bsize * bptr;

                // is it indexed doubly indirectly?
                } else if (offset < size_in_diblks) {
                        size_t this_offset = offset - size_in_iblks;

                        __u32 *diblock = (void*)(img + bsize
                                      * in->i_block[EXT2_DIND_BLOCK]);

                        __u32 ibptr = diblock[this_offset/(ppb * bsize)];
                        __u32 *iblock = (void*)(img + bsize * ibptr);

                        __u32 bptr = iblock[(this_offset%(ppb * bsize))
                                            /bsize];
                        block = (void*)(img + bsize * bptr);

                // is it indexed triply indirectly?
                } else if (offset < size_in_tiblks) {
                        size_t this_offset = offset - size_in_diblks;
                        
                        // grab the triply indirct block from the inode
                        __u32 *tiblock = (void*)(img + bsize
                                        * in->i_block[EXT2_TIND_BLOCK]);

                        __u32 dibptr = tiblock[this_offset/
                                               (ppb * ppb * bsize)];
                        __u32 *diblock = (void*)(img + bsize * dibptr);

                        __u32 ibptr = diblock[(this_offset%
                                               (ppb * ppb * bsize))/
                                              (ppb * bsize)];
                        __u32 *iblock = (void*)(img + bsize * ibptr);


                        __u32 bptr = iblock[this_offset/bsize];
                        block = img + bsize * bptr;

                // otherwise our offset is too large, oops
                } else {
                        assert(false);
                }
        }
}

static void __summarize_indirect(struct ext2_super_block *sb,
                                 struct ext2_inode *in, size_t inode_nr,
                                 uint8_t *img, __u32 bptr, int lvl,
                                 size_t offset)
{
        size_t bsize = EXT2_MIN_BLOCK_SIZE << sb->s_log_block_size;
        size_t ppb = bsize/sizeof(__u32);
        __u32 *blk = (void*)(img + bsize * bptr);

        size_t offset_shift = bsize;
        for (int i = 1; i < lvl; ++i)
                offset_shift *= ppb;

        for (size_t i = 0; i < ppb; ++i) {
                __u32 ptr = blk[i];
                if (ptr == 0)
                        continue;

                if (lvl != 1)
                        __summarize_indirect(sb, in, inode_nr, img, blk[i],
                                             lvl-1, offset + offset_shift*i);
                else {
                        printf("INDIRECT,%lu,%d,%lu,%u,%u\n",
                               inode_nr,
                               lvl,
                               (offset + offset_shift*i)/bsize,
                               bptr,
                               ptr);
                }
        }
}

static void summarize_indirect(struct ext2_super_block *sb,
                               struct ext2_inode *i, size_t inode_nr,
                               uint8_t *img)
{
        size_t bsize = EXT2_MIN_BLOCK_SIZE << sb->s_log_block_size;
        size_t ppb = bsize/sizeof(__u32);
        const size_t size_in_dblks = EXT2_NDIR_BLOCKS * bsize;
        const size_t size_in_iblks = ppb * bsize + size_in_dblks;
        const size_t size_in_diblks = ppb * ppb * bsize
                + size_in_iblks;
        
        if (i->i_block[EXT2_IND_BLOCK] != 0)
                __summarize_indirect(sb, i, inode_nr, img,
                                     i->i_block[EXT2_IND_BLOCK],
                                     1, size_in_dblks);

        if (i->i_block[EXT2_DIND_BLOCK] != 0)
                __summarize_indirect(sb, i, inode_nr, img,
                                     i->i_block[EXT2_DIND_BLOCK],
                                     2, size_in_iblks);

        if (i->i_block[EXT2_TIND_BLOCK] != 0)
                __summarize_indirect(sb, i, inode_nr, img,
                                     i->i_block[EXT2_TIND_BLOCK],
                                     3, size_in_diblks);
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

        // print block group summary

        // thanks to Adam for this more robust way of calculating the number
        // of block groups
        size_t nr_blk_grps = (sb->s_blocks_count
                              + sb->s_blocks_per_group - 1)
                              / sb->s_blocks_per_group;

        struct ext2_group_desc *grps = (void*)(img + bsize *
                                               (sb->s_first_data_block + 1));

        // the last block group might have a non-standard number of blocks
        // if the fs size does not evenly divide the block group 
        size_t blocks_in_last_group = sb->s_blocks_per_group;
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

        // print free block summary
        for (size_t i = 0; i < nr_blk_grps; ++i) {
                struct ext2_group_desc *bg = grps + i;
                uint8_t *bmap = img + bg->bg_block_bitmap*bsize;
                size_t blocks = i == nr_blk_grps - 1 ? blocks_in_last_group
                        : sb->s_blocks_per_group;
                
                for (size_t j = 0; j < blocks; ++j) {
                        // if the bit is 0, the block is free
                        if ((bmap[j/8] & (1 << (j%8))) == 0)
                                printf("BFREE,%lu\n",
                                       i*sb->s_blocks_per_group + j
                                       + (i == 0 ? sb->s_first_data_block : 0));
                }
        }

        // print free inode summary
        size_t inodes_in_last_group = sb->s_inodes_per_group;
        if (sb->s_inodes_count%sb->s_inodes_per_group != 0)
                inodes_in_last_group = sb->s_inodes_count
                        % sb->s_inodes_per_group;

        for (size_t i = 0; i < nr_blk_grps; ++i) {
                struct ext2_group_desc *bg = grps + i;
                uint8_t *imap = img + bg->bg_inode_bitmap*bsize;
                size_t inodes_this_grp = i == nr_blk_grps - 1
                        ? sb->s_inodes_per_group
                        : inodes_in_last_group;

                size_t inode_nr_start = i*sb->s_inodes_per_group + 1;

                for (size_t j = 0; j < inodes_this_grp; ++j) {
                        if ((imap[j/8] & (1 << (j%8))) == 0)
                                printf("IFREE,%lu\n",inode_nr_start + j);
                }
        }

        // print the inode summary
        for (size_t i = 0; i < nr_blk_grps; ++i) {
                struct ext2_group_desc *bg = grps + i;
                struct ext2_inode *itable = (void*)(
                        img + bg->bg_inode_table*bsize);
                size_t inodes_this_grp = i == nr_blk_grps - 1
                        ? sb->s_inodes_per_group
                        : inodes_in_last_group;

                size_t inode_nr_start = i*sb->s_inodes_per_group + 1;
                for (size_t j = 0; j < inodes_this_grp; ++j) {
                        struct ext2_inode *i = itable + j;

                        // invalid inode
                        if (!i->i_mode || !i->i_links_count)
                                continue;

                        
                        char type;
                        switch (i->i_mode & EXT2_MODE_FORMAT_MASK) {
                        case EXT2_S_IFREG:
                                type = 'f';
                                break;
                        case EXT2_S_IFLNK:
                                type = 's';
                                break;
                        case EXT2_S_IFDIR:
                                type = 'd';
                                break;
                        default:
                                type = '?';
                        }
                        
                        printf("INODE,");
                        printf("%lu,", inode_nr_start + j);
                        printf("%c,", type);
                        printf("%ho,", (short)(i->i_mode & 0xfff));
                        printf("%hu,", i->i_uid);
                        printf("%hu,", i->i_gid);
                        printf("%hu,", i->i_links_count);

                        // month shift hackery, don't worry about it
                        int msh = 1;
                        time_t tmp = i->i_ctime;
                        struct tm *t = gmtime(&tmp);
                        printf("%02d/%02d/%02d %02d:%02d:%02d,",
                               t->tm_mon+msh, t->tm_mday, t->tm_year % 100,
                               t->tm_hour, t->tm_min, t->tm_sec);

                        tmp = i->i_mtime;
                        t = gmtime(&tmp);
                        printf("%02d/%02d/%02d %02d:%02d:%02d,",
                               t->tm_mon+msh, t->tm_mday, t->tm_year % 100,
                               t->tm_hour, t->tm_min, t->tm_sec);

                        tmp = i->i_atime;
                        t = gmtime(&tmp);
                        printf("%02d/%02d/%02d %02d:%02d:%02d,",
                               t->tm_mon+msh, t->tm_mday, t->tm_year % 100,
                               t->tm_hour, t->tm_min, t->tm_sec);

                        printf("%u,", i->i_size);
                        printf("%u,", i->i_blocks);
                        for (size_t k = 0; k < EXT2_N_BLOCKS; ++k)
                                printf("%u%c", i->i_block[k],
                                       k == EXT2_N_BLOCKS - 1 ? '\n' : ',');
                }
        }

        // print the directory summary
        for (size_t i = 0; i < nr_blk_grps; ++i) {
                struct ext2_group_desc *bg = grps + i;
                struct ext2_inode *itable = (void*)(
                        img + bg->bg_inode_table*bsize);
                size_t inodes_this_grp = i == nr_blk_grps - 1
                        ? sb->s_inodes_per_group
                        : inodes_in_last_group;

                size_t inode_nr_start = i*sb->s_inodes_per_group + 1;
                for (size_t j = 0; j < inodes_this_grp; ++j) {
                        struct ext2_inode *i = itable + j;

                        // invalid inode
                        if (!i->i_mode || !i->i_links_count)
                                continue;
                        
                        if ((i->i_mode & EXT2_MODE_FORMAT_MASK) ==
                            EXT2_S_IFDIR)
                                summarize_directory(sb, i,
                                                    inode_nr_start + j, img);

                        if ((i->i_mode & EXT2_MODE_FORMAT_MASK) == EXT2_S_IFDIR
                            || (i->i_mode & EXT2_MODE_FORMAT_MASK) == EXT2_S_IFREG)
                                summarize_indirect(sb, i, inode_nr_start + j, img);
                }
        }
}
