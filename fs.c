// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"

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
  i8 wholeBuffer[BYTESPERDISK];
  i32 fbn = bfsTell(fd) / BYTESPERBLOCK;
  i32 inum = bfsFdToInum(fd);
  if (numb > BYTESPERBLOCK)
  {
    i8 blockBuffer[BYTESPERBLOCK];
    for (int i = 0; i < (numb / BYTESPERBLOCK); i++)
    {
      bfsRead(inum, fbn + i, blockBuffer);
      for (int j = 0; j < BYTESPERBLOCK; j++)
      {
        wholeBuffer[(BYTESPERBLOCK * i) + j] = blockBuffer[j];
      }
    }
    if (numb % BYTESPERBLOCK != 0)
    {
      bfsRead(inum, fbn + (numb / BYTESPERBLOCK), blockBuffer);
      for (int j = 0; j < (numb % BYTESPERBLOCK); j++)
      {
        wholeBuffer[(BYTESPERBLOCK * (numb / BYTESPERBLOCK)) + j] = blockBuffer[j];
      }
    }
    memcpy(buf, wholeBuffer, numb);
    int noWriting = 0;
    bfsRead(inum, fbn + (numb / BYTESPERBLOCK) - 1, blockBuffer);
    for (int i = 0; i < BYTESPERBLOCK; i++)
    {
      if(blockBuffer[i] == 0)
      {
        noWriting++;
      }
    }
    numb -= noWriting;
  }
  else
  {
    bfsRead(inum, fbn, wholeBuffer);
    memcpy(buf, wholeBuffer, numb);
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
  i8 wholeBuffer[2000];
  memcpy(wholeBuffer, buf, 2000);
  i32 fbn = bfsTell(fd) / BYTESPERBLOCK;
  i32 inum = bfsFdToInum(fd);
  i32 dbn = bfsFbnToDbn(inum, fbn);
  i8 blockBuffer[BYTESPERBLOCK] = {0};
  if (dbn >= 0)
  {
    bfsRead(inum, fbn, blockBuffer);
  }
  else
  {
    bfsAllocBlock(inum, fbn);
    dbn = bfsFbnToDbn(inum, fbn);
    memset(blockBuffer, 0, BYTESPERBLOCK);
  }
  if (numb > BYTESPERBLOCK)
  {
    int remainder = numb;
    for (int i = 0; i + (fsTell(fd) % BYTESPERBLOCK) < BYTESPERBLOCK; i++)
    {
      blockBuffer[i + (fsTell(fd) % BYTESPERBLOCK)] = wholeBuffer[i];
      remainder--;
    }
    bioWrite(dbn, blockBuffer);
    while (remainder > BYTESPERBLOCK)
    {
      for (int j = 0; j < BYTESPERBLOCK; j++)
      {
        blockBuffer[j] = wholeBuffer[numb - remainder];
        remainder--;
      }
      fbn++;
      dbn = bfsFbnToDbn(inum, fbn);
      bioWrite(dbn, blockBuffer);
    }
    dbn = bfsFbnToDbn(inum, fbn + 1);
    if (dbn >= 0)
    {
      bfsRead(inum, fbn + 1, blockBuffer);
    }
    else
    {
      bfsAllocBlock(inum, fbn + 1);
      dbn = bfsFbnToDbn(inum, fbn + 1);
      memset(blockBuffer, 0, BYTESPERBLOCK);
    }
    for (int i = 0; i < remainder ; i++)
    {
      blockBuffer[i] = wholeBuffer[numb - remainder + i];
    }
    bioWrite(dbn, blockBuffer);
  }
  else
  {
    for (int i = 0; i < numb; i++)
    {
      blockBuffer[(fsTell(fd) % BYTESPERBLOCK) + i] = wholeBuffer[i];
    }
    bioWrite(dbn, &blockBuffer);
  }
  fsSeek(fd, numb, SEEK_CUR);
  return 0;
}
