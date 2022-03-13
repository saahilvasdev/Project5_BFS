/*
Saahil Vasdev & Tommy Ni 
CSS430 - Operating System
Professor Dimpsey

Project 5 - Filesystem (BFS)

This C programs primary objective is to implement a Bothell
File System (BFS) that has similar functions to an unix-like
file system. We were responsible of implementing fsRead() and
fsWrite() that incorporated 3 layers: fs, bfs, bio. 

*/

// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"
#include <stdbool.h>

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) { 
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0; 
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {
  i32 size = fsSize(fd); //get the size of the fd
  i32 inum = bfsFdToInum(fd); //turns the fd to an inum
  i32 ofte = bfsFindOFTE(inum); //find ofte
  i32 cursor = fsTell(fd);  //gets the current cursor

  i32 startingFBN = cursor / BYTESPERBLOCK; //Calculates the startingFBN block
  i32 lastFBN = size / BYTESPERBLOCK;   //Calculates the lastFBN block
  i32 lastRequiredByte = cursor + numb;   //Calculates the last byte we will be reading

  //If the lastByte is bigger than file(out of bound), than set it to size
  if(size < lastRequiredByte){
    lastRequiredByte = size;
    numb = size - cursor;
  }
  //If lastRequiredFBN is more than the fbn of file, than set it to the last fbn
  i32 lastRequiredFBN = lastRequiredByte / BYTESPERBLOCK;
  if(lastFBN < lastRequiredFBN){
    lastRequiredFBN = lastFBN;
  }

  i32 cursorIndex = cursor - (startingFBN * BYTESPERBLOCK);
  i8 bufferBlock[BYTESPERBLOCK];
  //If we are requesting to read only the last block, than read last block only
  if(startingFBN == lastRequiredFBN){
    int ret = bfsRead(inum, startingFBN, bufferBlock);
    if(ret != 0) FATAL(ENYI);
    memcpy(buf, bufferBlock + cursorIndex, numb);
    fsSeek(fd, numb, SEEK_CUR);
    return numb;
  }

  i32 currentOffset = 0;
  i32 bufferBlockOffset = 0;
  i32 sizeOfCopy = BYTESPERBLOCK;
  i32 fbn = startingFBN;
  //Go through each fbn to read
  while(fbn <= lastRequiredFBN){
    int ret = bfsRead(inum, fbn, bufferBlock);
    if(ret != 0) FATAL(EBADREAD);

    //If its the first block
    if(fbn == startingFBN){
      bufferBlockOffset = cursorIndex;
      sizeOfCopy = BYTESPERBLOCK - cursorIndex;
    }

    //If its the last block
    if(fbn == lastRequiredFBN){
      //Calculates the last few bytes that is in the last block to be read
      sizeOfCopy = (numb - (BYTESPERBLOCK - cursorIndex)) % BYTESPERBLOCK;
      //Of the sizeOfCopy is somehow 0, we subtract a FBN from the variable
      if(sizeOfCopy == 0) lastRequiredFBN--;
    }

    //Copies data into buf + currentOffset
    memcpy(buf + currentOffset, bufferBlock + bufferBlockOffset, sizeOfCopy);
    //If it is the first block, than add sizeOfCopy to currentOffset
    if(fbn == startingFBN){
      currentOffset += sizeOfCopy;
    } else {
      //Otherwise we add 512 to currentOffset
      currentOffset += BYTESPERBLOCK;
    }
    bufferBlockOffset = 0;
    sizeOfCopy = BYTESPERBLOCK;
    fbn++;  //increment fbn
  }

  fsSeek(fd, numb, SEEK_CUR);
  return numb;
}


// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);
 
  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);
  
  switch(whence) {
    case SEEK_SET:
      g_oft[ofte].curs = offset;
      break;
    case SEEK_CUR:
      g_oft[ofte].curs += offset;
      break;
    case SEEK_END: {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
      }
    default:
        FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}


// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {
  i32 size = fsSize(fd); //get the size of the fd
  i32 inum = bfsFdToInum(fd); //turns the fd to an inum
  i32 ofte = bfsFindOFTE(inum); //find ofte
  i32 cursor = fsTell(fd);  //gets the current cursor

  i32 currentFBN = cursor / BYTESPERBLOCK;  //Gets the currentFBN block
  i32 lastFBN = size / BYTESPERBLOCK;   //Gets the last FBN block

  i32 bytesWritten = 0;   //Holds the number of bytes we have already written
  i32 trailBytes = 0;     //Holds the number of bytes available for a block to be written
  i32 bytesToWrite = 0;   //Holds the number of bytes we need to write
  i32 cursorBlockIndex = 0;   //Holds the index of the cursor's block
  i32 copyNumb = numb;    //Holds numb, this variable will be modified later

  i8 temporaryBuffer[BYTESPERBLOCK];  //Will hold the current disk's data
  i8 numberBytesToWrite[BYTESPERBLOCK]; //Will hold the buf's data

  //If we need to write more than there is space in the existing file, extend the file
    if(cursor + numb > size){
      i32 totalSize = cursor + numb;
      i32 addingBlocks = (totalSize / BYTESPERBLOCK) + 1;
      bfsExtend(inum, addingBlocks);
      bfsSetSize(inum, totalSize);
    }

  //We write one block at a time to the disk
  //If the number of bytes we still need to write is 0, than we have finish writing everything, leave while loop
  while(copyNumb != 0){
    cursorBlockIndex = cursor - (currentFBN * BYTESPERBLOCK); //keep track of the cursor index
    trailBytes = BYTESPERBLOCK - cursorBlockIndex;  //keep tracks of number of bytes left in block available to write
    //Determines what bytesToWrite variable is set to
    if(trailBytes > copyNumb){  //Goes into this if statement if there is less bytes to write than there is space
      bytesToWrite = copyNumb;
    }
    else{   //Goes into this if there is more bytes to be written than there is space in this block
      bytesToWrite = trailBytes;
    }

    //Resets numberBytesToWrite to all null
    memset(numberBytesToWrite, 0, sizeof(numberBytesToWrite));
    //Reads current block into buffer
    bfsRead(inum, currentFBN, temporaryBuffer);
    //Copy the bytes to be written from buf into numberBytesToWrite
    memcpy(numberBytesToWrite, (buf + bytesWritten), bytesToWrite);
    //Copy the bytes to be written into another char buffer but add the cursorIndex
    memcpy((temporaryBuffer + cursorBlockIndex), numberBytesToWrite, bytesToWrite);
    //Moves the cursor a number of bytes forward
    fsSeek(fd, bytesToWrite, SEEK_CUR);
    cursor = fsTell(fd);

    //Subtract the number of bytes we just wrote from numb
    copyNumb = copyNumb - bytesToWrite;
    //Add the number of bytes we just wrote to bytesWritten
    bytesWritten = bytesWritten + bytesToWrite;

    //Find the currentDBN on disk and write the temporaryBuffer into the actual block on disk
    int currentDBN = bfsFbnToDbn(inum, currentFBN);
    bioWrite(currentDBN, temporaryBuffer);

    //Increment to the next fbn
    currentFBN++;
  }

  //FATAL(ENYI);                                  // Not Yet Implemented!
  return bytesWritten;
}
