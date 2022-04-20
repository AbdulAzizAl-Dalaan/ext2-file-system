/************* read_write.c file **************/

#include <sys/stat.h>
#include <ext2fs/ext2fs.h>
#include <fcntl.h>
#include <libgen.h>
#include <time.h>
#include <string.h>
#include "commands.h"
#include "type.h"
#include "util.h"

int myread(int fd, int nbytes,  char* buf)
{
    // checks if the fd is valid
    if (fd < 0 || fd >= NFD || running->fd[fd] == NULL)
    {
        printf("myread: fd is invalid\n");
        return -1;
    }

    // checks if the file is open for READ
    if (running->fd[fd]->mode == READ || running->fd[fd] == READ_WRITE)
    {
        printf("myread: fd is not open for read\n");
        return -1;
    }

    char read_buf[BLKSIZE];
    int count = 0, blk = 0, remain = 0;
    // offset is set to byte set in file to read
    OFT *oftp = running->fd[fd];
    MINODE* mip = oftp->minode_ptr;
    int offset = oftp->offset; 

    // compute bytes available in file: avil_bytes = fileSize - offset
    int avil_bytes = oftp->minode_ptr->INODE.i_size - offset;

    char *cq = buf;

    while(nbytes && avil_bytes)
    {
        int lbk = oftp->offset / BLKSIZE;
        int startByte = oftp->offset % BLKSIZE;

        if (lbk < 12)
        {
            blk = oftp->minode_ptr->INODE.i_block[lbk];
        }
        // Indirect Block
        else if (lbk >= 12 && lbk < 256 + 12)
        {
            get_block(oftp->minode_ptr->dev, oftp->minode_ptr->INODE.i_block[12], read_buf);
            blk = ((int*)read_buf)[lbk - 12];
        }
        // Double Indirect
        else
        {
            get_block(mip->dev, mip->INODE.i_block[13], read_buf);
            int *indirect_blk = (int*)read_buf;

            // BLKSIZE = 1024
            // sizeof(int) = 4
            // res = 256
            const int res = BLKSIZE / sizeof(int);
            const int indirect_blk_num = lbk - (12 + res);
            blk = indirect_blk[indirect_blk_num / res];
            get_block(mip->dev, blk, read_buf);
            blk = ((int*)read_buf)[indirect_blk_num % res];
        }

        // get the data block into readbuf[BLKSIZE]
        get_block(oftp->minode_ptr->dev, blk, read_buf);

        // copy from startByte to buf[ ], at most remain bytes in this block
        char *cp = read_buf + startByte;
        remain = BLKSIZE - startByte;

        // What is this trying to do?
        // if (nbytes < remain) remain = nbytes;
        // TODO: Why is this here?
        while(remain > 0)
        {
            *cq++ = *cp++;
            oftp->offset++;
            count++;
            avil_bytes--; nbytes--; remain--;
            if (nbytes <= 0 || avil_bytes <= 0)
                break; 
        }

        // if one data block is not enough, loop back to OUTER while for more ...
    }

    printf("myread: read %d char from file descriptor %d\n", count, fd);
    return count;
}

int mywrite(int fd, int nbytes, char *buf)
{
    // checks if the fd is valid
    if (fd < 0 || fd >= NFD || running->fd[fd] == NULL)
    {
        printf("myread: fd is invalid\n");
        return -1;
    }

    // checks if the file is open for WRITE
    if ((running->fd[fd]->mode == WRITE || running->fd[fd] == READ_WRITE) == 0)
    {
        printf("mywrite: fd is not open for write\n");
        return -1;
    }

    char write_buf[BLKSIZE];
    char *cq = buf;
    int blk = 0, count = 0, remain = 0;
    OFT *oftp = running->fd[fd];
    MINODE *mip = running->fd[fd]->minode_ptr;
    while (nbytes > 0)
    {
        int lbk = oftp->offset / BLKSIZE;
        const int startByte = oftp->offset % BLKSIZE;

        if (lbk < 12)
        {
            if(mip->INODE.i_block[lbk] == 0)
            {
                // allocate a new data block
                mip->INODE.i_block[lbk] = balloc(mip->dev);
            }
            int blk = mip->INODE.i_block[lbk];
        }
        else if (lbk >= 12 && lbk < 256 + 12)
        {
            // Indirect Blocks
            if(mip->INODE.i_block[12] == 0)
            {
                // allocate a new block for it
                // zero out the block on the disk
                mip->INODE.i_block[12] = balloc(mip->dev);
                if (mip->INODE.i_block[12] == 0)
                {
                    printf("mywrite: no more blocks\n");
                    return -1;
                }
                get_block(mip->dev, mip->INODE.i_block[12], write_buf);
                const int res = BLKSIZE / sizeof(int);
                for (int i = 0; i < res; i++)
                {
                    ((int*)write_buf)[i] = 0;
                }
                put_block(mip->dev, mip->INODE.i_block[12], write_buf);
                mip->INODE.i_blocks++;
            }
            // get i_block[12] into an int ibuf[256]
            char ibuf[BLKSIZE];
            get_block(mip->dev, mip->INODE.i_block[12], ibuf);
            blk = ibuf[lbk - 12];
            if (blk == 0)
            {
                // allocate a disk block
                // record it in i_block[12]
                blk = ibuf[lbk - 12] = balloc(mip->dev);
                mip->INODE.i_blocks++;
                put_block(mip->dev, mip->INODE.i_block[12], ibuf);
            }
            // ....... more to do after???
        }
        else
        {
            // Double Indirect Blocks
            lbk -= (12 + (BLKSIZE / sizeof(int)));
            if (mip->INODE.i_block[13] == 0)
            {
                // allocate a new block for it
                // zero out the block on the disk
                mip->INODE.i_block[13] = balloc(mip->dev);
                if (mip->INODE.i_block[13] == 0)
                {
                    printf("mywrite: no more blocks\n");
                    return -1;
                }
                get_block(mip->dev, mip->INODE.i_block[13], write_buf);
                const int res = BLKSIZE / sizeof(int);
                for (int i = 0; i < res; i++)
                {
                    ((int*)write_buf)[i] = 0;
                }
                put_block(mip->dev, mip->INODE.i_block[13], write_buf);
                mip->INODE.i_blocks++;
            }

            char indirect_blk[BLKSIZE / sizeof(int)];
            get_block(mip->dev, mip->INODE.i_block[13], indirect_blk);
            const int res = BLKSIZE / sizeof(int);
            int indirect_blk_num = indirect_blk[lbk / res];
            if (indirect_blk_num == 0)
            {
                // allocate a disk block
                // record it in i_block[13]
                indirect_blk_num = indirect_blk[lbk / res] = balloc(mip->dev);
                for (int i = 0; i < res; i++)
                {
                    ((int*)indirect_blk)[i] = 0;
                }
                mip->INODE.i_blocks++;
                put_block(mip->dev, mip->INODE.i_block[13], indirect_blk);
                put_block(mip->dev, indirect_blk_num, write_buf);
            }
            // get i_block[13] into an int indirect_blk[256]
            get_block(mip->dev, indirect_blk_num, indirect_blk);
            blk = indirect_blk[lbk % res];
            if (blk == 0)
            {
                // allocate a disk block
                // record it in i_block[13]
                blk = indirect_blk[lbk % res] = balloc(mip->dev);
                mip->INODE.i_blocks++;
                put_block(mip->dev, indirect_blk_num, indirect_blk);
            }
        }
        // for all cases come here to write to the data block

        get_block(mip->dev, blk, write_buf);
        // copy from startByte to buf[ ], at most remain bytes in this block
        char *cp = write_buf + startByte;
        remain = BLKSIZE - startByte;

        // Why this here?
        // We need to write the data to the block
        // The outer while loop will keep writing until we have written all the bytes
        if (remain < nbytes + 1)
        {
            // copy from buf[ ] to write_buf[ ]
            memcpy(cp, cq, remain);
            // write the block back to disk
            put_block(mip->dev, blk, write_buf);
            // update the offset
            oftp->offset += remain;
            // update the count
            count += remain;
            // update the nbytes
            nbytes -= remain;
            // update the buf
            cq += remain;
        }
        else
        {
            // copy from buf[ ] to write_buf[ ]
            memcpy(cp, cq, nbytes);
            // write the block back to disk
            put_block(mip->dev, blk, write_buf);
            // update the offset
            oftp->offset += nbytes;
            // update the count
            count += nbytes;
            // update the nbytes
            nbytes -= nbytes;
            // update the buf
            cq += nbytes;
        }
        put_block(mip->dev, blk, write_buf);
        // loop back to outer while to write more ... until nbytes are written.
    }

    mip->dirty = 1;
    printf("wrote %d char into file descriptor %d\n", nbytes, fd);
    return nbytes;
}

int map(MINODE *mip, int lbk)
{
    int ibuf[256];
    int blk = 0;
    if (lbk < 12)
    {
        blk = mip->INODE.i_block[lbk];
    }
    else if (blk >= 12 && blk < 256 + 12)
    {
        // read INODE.i_block[12] into ibuf[256]
        get_block(mip->dev, ip->i_block[12], ibuf);
        blk = ibuf[lbk - 12];
    }
    else
    {
        // TODO: Double Indirect Blocks
        // this algorithm needs to be completed by using mailman's algorithm
        // to convert double indirect logical blocks to physical blocks

    }
    return blk;
}