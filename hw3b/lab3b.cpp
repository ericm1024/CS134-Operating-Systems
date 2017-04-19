// lab3b.c
//
// NAME: Eric Mueller
// EMAIL: emueller@hmc.edu
// ID: 40160869

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

using namespace std;

// a reference to a block
struct blk_ref {
        blk_ref(uint32_t _offset, uint32_t _ino, int _lvl)
                : offset{_offset}, ino{_ino}, lvl{_lvl}
        {}
        
        // offset in file
        uint32_t offset = 0;
        // inode number refering to this
        uint32_t ino = 0;
        // level (0 = direct, 1 = ind, 2 = doub ind, 3 = trip ind)
        int lvl = 0;
};

// http://stackoverflow.com/a/236803/3775803
template<typename Out>
void split(const std::string &s, char delim, Out result) {
        std::stringstream ss;
        ss.str(s);
        std::string item;
        while (std::getline(ss, item, delim))
                *(result++) = item;
}

std::vector<std::string> split(const std::string &s, char delim = ',') {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}

int main(int argc, char **argv)
{
        size_t nr_blks = 0;
        size_t nr_inodes = 0;
        uint32_t blk_size = 0;
        size_t first_free_ino = 0;
        size_t i_size = 0;
        
        // bitmap for allocated blocks. Indexed by block number.
        vector<bool> b_alloc_bmp;

        // references we find to teach block. Indexed by block num
        unordered_multimap<uint32_t, blk_ref> b_refs;

        // reserved blocks
        unordered_set<uint32_t> b_reserved;

        // inode allocation bitmap. Indexed by inode number. True --> allocd
        vector<bool> i_alloc_bmp;

        // number of references we find to each inode
        vector<uint32_t> i_counts;

        // given i_links_count count we find in each inode
        vector<uint32_t> i_counts_given;

        // did we see this inode?
        vector<bool> i_seen;

        if (argc != 2) {
                cerr << "usage: ./lab3b <filename>" << endl;
                exit(1);
        }

        ifstream file{argv[1]};
        string line;
        bool found_sb = false;

        // scan through the file lookin' for the superblock
        while (getline(file, line)) {
                string prefix{"SUPERBLOCK"};

                if (line.compare(0, prefix.size(), prefix) != 0)
                        continue;

                auto fields = split(line);

                nr_blks = atol(fields.at(1).c_str());
                nr_inodes = atol(fields.at(2).c_str());
                blk_size = atol(fields.at(3).c_str());
                i_size = atol(fields.at(4).c_str());
                first_free_ino = atol(fields.at(7).c_str());
                found_sb = true;
                break;
        }

        if (!found_sb) {
                cerr << "bad file: could not find SUPERBLOCK entry" << endl;
                exit(1);
        }

        // resize all of our vectors now that we know how big they need to be
        b_alloc_bmp.resize(nr_blks);
        // add 1 to the inode things because inode numbers start at 1 for some
        // fucking reason
        i_alloc_bmp.resize(nr_inodes + 1);
        i_counts.resize(nr_inodes + 1);
        i_counts_given.resize(nr_inodes + 1);
        i_seen.resize(nr_inodes + 1);

        // fill our allocation bitmaps with true (allocated) so
        // we can then wipe them with {B,I}FREE entries, and fill our inode
        // refcounts with 
        fill(b_alloc_bmp.begin(), b_alloc_bmp.end(), true);
        fill(i_alloc_bmp.begin(), i_alloc_bmp.end(), true);
        fill(i_counts.begin(), i_counts.end(), 0);
        fill(i_counts_given.begin(), i_counts_given.end(), 0);
        fill(i_seen.begin(), i_seen.end(), false);

        // go back to the beginning of the file
        file.clear();
        file.seekg(0, ios::beg);

        // boot block, superblock, and group descriptor
        b_reserved.insert(0);
        b_reserved.insert(1);
        b_reserved.insert(2);

        while (getline(file, line)) {
                string prefix{"GROUP"};

                if (line.compare(0, prefix.size(), prefix) != 0)
                        continue;

                auto fields = split(line);

                // the block bitmap, inode bitmap, and inode tables for
                // each group are reserved
                uint32_t nr_inodes = atol(fields.at(3).c_str());
                uint32_t nr_iblks = (nr_inodes*i_size + (blk_size - 1))
                                    /blk_size;
                uint32_t fbb_blk = atol(fields.at(6).c_str());
                uint32_t fib_blk = atol(fields.at(7).c_str());
                uint32_t itb_blk = atol(fields.at(8).c_str());

                b_reserved.insert(fbb_blk);
                b_reserved.insert(fib_blk);
                for (uint32_t blk = itb_blk; blk < itb_blk + nr_iblks; ++blk)
                        b_reserved.insert(blk);

                // XXX: this doesn't handle multiple groups. There is
                // reserved space at the beginning of some groups for
                // backups of the superblock and group descriptor tables
        }

        // go back to the beginning of the file
        file.clear();
        file.seekg(0, ios::beg);

        // scan through the file looking for free entries
        while (getline(file, line)) {
                string prefix{"FREE"};

                // look for BFREE or IFREE, otherwise keep going
                if (line.compare(1, prefix.size(), prefix) != 0)
                        continue;
                
                auto fields = split(line);
                size_t ino = atol(fields.at(1).c_str());

                if (line.at(0) == 'B') {
                        b_alloc_bmp.at(ino) = false;
                } else if (line.at(0) == 'I') {
                        i_alloc_bmp.at(ino) = false;
                } else {
                        cerr << "bad line in file: \"" << line << "\""
                             << endl;
                        exit(1);
                }
        }

        // go back to the beginning of the file
        file.clear();
        file.seekg(0, ios::beg);

        // scan through the file looking for references to blocks, i.e.
        // INODE or INDIRECT entries
        while (getline(file, line)) {
                // common prefix of INODE and INDIRECT
                string prefix{"IN"};

                if (line.compare(0, prefix.size(), prefix) != 0)
                        continue;

                auto fields = split(line);

                // we found an inode! add each block ref it contains to
                // our block ref map
                if (fields.at(0) == "INODE") {
                        uint32_t ino = atol(fields.at(1).c_str());

                        i_seen.at(ino) = true;

                        // while we're here, store the refcount we found in
                        // this inode
                        uint32_t count = atol(fields.at(6).c_str());

                        i_counts_given.at(ino) = count;
                        
                        // the INODE entries have block numbers starting
                        // at field 12 and going for 15 fields
                        for (size_t i = 12; i < 27; ++i) {
                                uint32_t blk = atol(fields.at(i).c_str());

                                // null block pointer
                                if (blk == 0)
                                        continue;
                                
                                // what level of indirection is this block?
                                int lvl;
                                switch (i) {
                                case 26:
                                        lvl = 3;
                                        break;
                                case 25:
                                        lvl = 2;
                                        break;
                                case 24:
                                        lvl = 1;
                                        break;
                                default:
                                        lvl = 0;
                                        break;
                                }

                                uint32_t off = 0;
                                int ppb = blk_size/4;
                                switch (i) {
                                case 26:
                                        off += ppb*ppb*blk_size;
                                case 25:
                                        off += ppb*blk_size;
                                default:
                                        off += (i >= 24 ? 12 : (i-12))
                                                  * blk_size;
                                }

                                b_refs.insert(make_pair(blk,
                                                        blk_ref{off, ino, lvl}));
                        }
                } else if (fields.at(0) == "INDIRECT") {
                        uint32_t ino = atol(fields.at(1).c_str());
                        int lvl = atoi(fields.at(2).c_str()) - 1;
                        uint32_t off = atol(fields.at(3).c_str());
                        uint32_t blk = atol(fields.at(5).c_str());

                        b_refs.insert(make_pair(blk, blk_ref{off, ino, lvl}));
                } else {
                        cerr << "bad line in file: \"" << line << "\""
                             << endl;
                        exit(1);
                }
        }

        // go back to the beginning of the file
        file.clear();
        file.seekg(0, ios::beg);

        // scan through the file looking for inode references. Also
        // audit directory entries while we're here
        while (getline(file, line)) {
                string prefix{"DIRENT"};

                if (line.compare(0, prefix.size(), prefix) != 0)
                        continue;

                auto fields = split(line);
                
                uint32_t dir_ino = atol(fields.at(1).c_str());
                uint32_t referent_ino = atol(fields.at(3).c_str());

                //i_counts.at(dir_ino) += 1;
                if (referent_ino < i_counts.size())
                        i_counts.at(referent_ino) += 1;

                auto name = fields.at(6);
                const char *state = NULL;
                
                if (referent_ino > nr_inodes)
                        state = "INVALID";
                else if (i_alloc_bmp.at(referent_ino) == false)
                        state = "UNALLOCATED";

                if (state && referent_ino != 2) {
                        printf("DIRECTORY INODE %u NAME %s %s INODE %u\n",
                               dir_ino,
                               name.c_str(),
                               state,
                               referent_ino);
                }

                // finding the parent inode number is annoying so lol I'm
                // not gonna bother
                if ((name == "'.'" && referent_ino != dir_ino)
                    || (name == "'..'" && dir_ino == 2 && referent_ino != dir_ino)) {
                        printf("DIRECTORY INODE %u NAME %s LINK TO INODE %u SHOULD BE %u\n",
                               dir_ino,
                               name.c_str(),
                               referent_ino,
                               dir_ino);
                }
        }

        // go back to the beginning of the file
        file.clear();
        file.seekg(0, ios::beg);

        // inode allocation audit
        while (getline(file, line)) {
                auto fields = split(line);

                if (fields.at(0) != "INODE")
                        continue;

                char type = fields.at(2).at(0);
                uint32_t ino = atol(fields.at(1).c_str());
                
                // valid inode, should be allocated
                if (type == 'f' || type == 'd' || type == 's') {
                        // oops, the bitmap doesn't reflect that
                        if (i_alloc_bmp.at(ino) == false)
                                printf("ALLOCATED INODE %u ON FREELIST\n",
                                       ino);

                // invalid inode, should not be allocated
                } else {
                        if (i_alloc_bmp.at(ino) == true)
                                printf("UNALLOCATED INODE %u NOT ON FREELIST\n",
                                       ino);
                }
                        
        }

        // audit for reserved and multiply referenced blocks
        for (uint32_t blk = 0; blk < nr_blks; ++blk) {
                // grab all references to this block
                auto range = b_refs.equal_range(blk);
                auto start = range.first;
                auto end = range.second;
                auto nrefs = distance(start, end);

                if (nrefs == 0)
                        continue;

                for (; start != end; ++start) {
                        auto bref = start->second;
                
                        // is this block reserved?
                        if (b_reserved.count(blk) > 0)
                                printf("RESERVED %sBLOCK %u IN INODE %u AT OFFSET %u\n",
                                       bref.lvl == 3 ? "TRIPPLE INDIRECT "
                                       : bref.lvl == 2 ? "DOUBLE INDIRECT "
                                       : bref.lvl == 1 ? "INDIRECT "
                                       : "",
                                       blk,
                                       bref.ino,
                                       bref.offset/blk_size);

                        // it should be marked as allocated
                        if (blk < nr_blks && nrefs > 0 && b_alloc_bmp.at(blk) != true)
                                printf("ALLOCATED BLOCK %u ON FREELIST\n", blk);

                        if (nrefs > 1) {
                                printf("DUPLICATE %sBLOCK %u IN INODE %u AT OFFSET %u\n",
                                       bref.lvl == 3 ? "TRIPPLE INDIRECT "
                                       : bref.lvl == 2 ? "DOUBLE INDIRECT "
                                       : bref.lvl == 1 ? "INDIRECT "
                                       : "",
                                       blk,
                                       bref.ino,
                                       bref.offset/blk_size);
                        }
                }
        }

        // audit for invalid blocks
        for (const auto entry : b_refs) {
                const auto blk = entry.first;
                const auto bref = entry.second;

                if (blk >= nr_blks)
                        printf("INVALID %sBLOCK %u IN INODE %u AT OFFSET %u\n",
                                       bref.lvl == 3 ? "TRIPPLE INDIRECT "
                                       : bref.lvl == 2 ? "DOUBLE INDIRECT "
                                       : bref.lvl == 1 ? "INDIRECT "
                                       : "",
                                       blk,
                                       bref.ino,
                                       bref.offset/blk_size);
        }

        // audit for unreferenced blocks
        for (size_t i = 0; i < nr_blks; ++i) {
                // if the block is unallocated, it is 'referenced' by the
                // free list, so we're good
                if (b_alloc_bmp.at(i) == false)
                        continue;

                // otherwise this block is allocated according to the bitmap,
                // so it better be referenced by someone or reserved
                if (b_refs.count(i) || b_reserved.count(i))
                        continue;

                // oops, otherwise it's an unreferenced block!
                printf("UNREFERENCED BLOCK %lu\n", i);
        }

        // audit inode link counts and more allocation shit
        // indexing is braindead and starts at 1 because ext2 inode numbers
        // start at 1...
        for (uint32_t ino = 0; ino <= nr_inodes; ++ino) {
                if (ino <= first_free_ino && ino != 2)
                        continue;
                
                if (i_seen.at(ino) && i_counts.at(ino) != i_counts_given.at(ino))
                        printf("INODE %u HAS %u LINKS BUT LINKCOUNT IS %u\n",
                               ino,
                               i_counts.at(ino),
                               i_counts_given.at(ino));

                // we never saw this inode and it doesn't appear on the freelist
                if (i_seen.at(ino) == false
                    && i_alloc_bmp.at(ino) == true) {
                        printf("UNALLOCATED INODE %u NOT ON FREELIST\n",
                               ino);
                }
        }
}
