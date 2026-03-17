#### Extending xv6 file system using DOUBLE INDIRECT
- increase max size of xv6 file
- currently limits to 268 blocks (BSIZE bytes) -> BSIZE is 1024 in xv6
- every file managed by index node like a fixed-size catalog card stored on disk with limited slots to list where file data blocks are
	- contains 12 DIRECT (12) + 1 SINGLE INDIRECT (256) = 268
- task is to remodel this inode to support 65803 blocks using DOUBLE INDIRECT
##### Steps to consider
1. Update constants
	- `fs.h`
		- reduce direct blocks from 12 to 11 so 1 slot can hold DOUBLE INDIRECT pointer
		- DOUBLE INDIRECT = SINGLE * SINGLE
		- MAXFILE = DIRECT + SINGLE + DOUBLE
		- changes to DINODE (on-disk inode) STRUCTURE to add a ptr to a ptr to a block of data for DOUBLE INDIRECT
			- NDIRECT + 2
	- `file.h` : inode structure update to `uint addrs[NDIRECT+2]` too; 
	
```c
<fs.h>
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+2];   // changed 
};
// addrs[0] to addrs[10] is for 11 actual direct data blocks
// addrs[11] is one block of single indirect filled with 256 address which points to more data
// addrs[12] is one block of double indirect with 256 addresses each pointing to another block of 256 addresses which finally points to data
```

2. Update `bmap` 
	- returns disk block address of nth block for file's logical block `bn`, allocates if no such block exists and returns 0 if out of disk space
		- if file says block 0 = first chunk of file, block 1 = second chunk, `bmap` figures out where on disk that chunk lives
		
	- task
		- check if block num is beyond DIRECT and SINGLE INDIRECT, and use DOUBLE INDIRECT logic
		- get address of DOUBLE block
		- load DOUBLE block (first level)
			- calculate index for first level
			- calculate index for second level
			- if indirect block does not exist, allocate it
		- load DOUBLE block (second level)
			- allocate actual data block
		
	- key terms
		- bn = logical block num inside file
		- ip = ptr to inode of the file
		- ip->addrs[] = array of block addresses stored in inode
		- addr = actual disk block number
		- `bread(device, block_num)`: fetches block from disk and put in mem buffer, can look inside to see addresses like a map book
		- `brelse(buffer)`: release locked buffer as kernel has small buffer cache, if open map book without closing, kernel will run out of space and crash
			- always call brelse as soon as address required is read
		- `balloc(device)`: allocates a zeroed disk block
		- `bfree(device, block)`: free disk block

```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;
  // direct
  if(bn < NDIRECT){ // is file block num within direct block range
    if((addr = ip->addrs[bn]) == 0){ // check if no block allocated to this slot
      addr = balloc(ip->dev); // alocate one free disk block here
      if(addr == 0) // allocation failed if 0, +ve block if success
        return 0; // cannot find free block, out of disk space
      ip->addrs[bn] = addr; 
      // store newly allocated block into inode
      // connects logical file block bn to physical disk block addr
      // example ip->addrs[3] = 147
    }
    return addr; // for bmap to return disk block for file block bn
  }
  bn -= NDIRECT; // only runs if DIRECT CHECK is false
  // if block not in direct region, move bn forward so it go next region
  // single
  if(bn < NINDIRECT){
    // load single blocks by loading that address into addr and checking if 0
    if((addr = ip->addrs[NDIRECT]) == 0){ // slot is not a data block, its inode entry storing address of single indirect block
      addr = balloc(ip->dev); // creates single indirect block
      if(addr == 0) // if 0 means file does not have indirect block yet
        return 0; // failed
      ip->addrs[NDIRECT] = addr; // allocate 
      // not final file data block
      // allocated index block that will store many data block addr
      // inode knows single indirect block is stored at disk block addr
    }
    // buffer bp = disk block in mem
    bp = bread(ip->dev, addr); // read indirect block from disk to mem
    a = (uint*)bp->data; // intepret data inside that buffer as array of unsigned integers as indirect block is not content text, its block numbers like a[0],a[1]
    if((addr = a[bn]) == 0){ // inside this indirect addr table, is there assigned data block? 0 means requires new data block allocation
      addr = balloc(ip->dev); // creates real file content block
      if(addr){ // if allocation success
        a[bn] = addr; // store newly allocated data block addr into indirect block entry, so let the entry bn point to disk block addr
        log_write(bp); // ensure change is committed correctly
      }
    }
    brelse(bp); // release buffer
    return addr;
  }
  bn -= NINDIRECT; // move bn forward to DOUBLE INDIRECT if false

  // DOUBLE INDIRECT
  if (bn < NDINDIRECT) {
    // divide logical/SINGLE to get which entry in root double indirect
    int first = bn / NINDIRECT; // first level of root block
    // which entry inside that chosen root block
    // go root entry, inside that level block, use entry xx
    int second = bn % NINDIRECT; // second level actual data block

	// inode entry stores address of DOUBLE INDIRECT root block
	// check if file already has that, if yes, use it
	// if not, allocate one and store it in inode
	// allocated block is not data block but ROOT INDEX BLOCK
	if ((addr = ip->addrs[NDIRECT + 1]) == 0) {  
		addr = balloc(ip->dev);  
		if(addr == 0)  
			return 0; // safer check
		ip->addrs[NDIRECT + 1] = addr;  
	}
	
    // read level 1 root block from disk into mem
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data; 

    if ((addr = a[first]) == 0) { // checks if data level block is allocated
	  // allocate new level-2 indirect block and store address in root block
      a[first] = addr = balloc(ip->dev);
      log_write(bp); // commit
    }
    brelse(bp); // remember to free after every bread

    // read level 2 block containing addr of actual data blocks
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
   
    if ((addr = a[second]) == 0) { // file has not allocated data block
      a[second] = addr = balloc(ip->dev); 
      log_write(bp); // final actual file content block
    }
    brelse(bp);
    return addr; // return physical disk block for logical file
  }
  panic("bmap: out of range"); // not direct, single or double
  // trying to access beyond MAXFILE
}
```

3. Update `itrunc`
	- task: cleanup/free all blocks referenced
		- direct blocks
			- for each direct ptr in array of address, if there is nonzero, call bfree and set ptr back to 0 to release actual data blocks
			
		- single indirect data blocks, then the indirect blocks that stored addr
			- if inode has single indirect, read that block, loop through all entries inside it, free each data block it points to and release the buffer to free the indirect block itself
			- clear inode pointer last
			
		- double indirect data blocks, then each secondary root block (second), then master map block (root)
			- if inode has double indirect, read root block, for each first-level entry, if nonzero, read that second-level block and free every data block listed inside, release that second-level block buffer and free that second-level index block itself
			- release root buffer to free root block itself
			- clear inode ptr

```c
// Truncate inode (discard contents).
// Caller must hold ip->lock.
void
itrunc(struct inode *ip) // ip is file whose content is now being erased
{
  int i, j; // outer and inner loop counter for nested layers
  struct buf *bp, *bp2; // buffer ptrs for root block, level-2 block
  uint *a, *a2; // ptrs to arrays of block addresses in the 2 blocks

  // direct
  // loop through every direct block slot in inode to ensure its 0
  // 0 means no block allocated there anymore
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){ // if direct ptr is nonzero
      bfree(ip->dev, ip->addrs[i]); // free that disk block that the direct slot is pointing to
      ip->addrs[i] = 0; // clear inode ptr after block freeing
      // prevents inode from still pointing at a released block
    }
  }

  // single indirect
  if(ip->addrs[NDIRECT]){ //nonzero for this to run, means indirect block still
    bp = bread(ip->dev, ip->addrs[NDIRECT]); // read single indirect from disk
    a = (uint*)bp->data; // intepret contents as array of uint addr
    for(j = 0; j < NINDIRECT; j++){ // loop every addr entry inside that indirect
      if(a[j]) // if entry j is nonzero means entry points to data block
        bfree(ip->dev, a[j]); // free that data block
    }
    brelse(bp); // release buffer for single indirect block, done reading entries
    bfree(ip->dev, ip->addrs[NDIRECT]); // free the block too
    ip->addrs[NDIRECT] = 0; // reset inode ptr
  }
  
  // double indirect
  if (ip->addrs[NDIRECT + 1]) { //check if holds the disk block num of DOUBLE
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]); // read DOUBLE root from disk
    // root block contains addr of level2-indirect
    // does not point directly to data blocks
    a = (uint*)bp->data;

    for (i = 0; i < NINDIRECT; i++) { 
    // loop through level2 block ptr IN ROOT BLOCK
      if (a[i]) { //if nonzero means got data block allocated to the root
        bp2 = bread(ip->dev, a[i]); // read the level2 indirect block
        a2 = (uint*)bp2->data;
       
        for (j = 0; j < NINDIRECT; j++)  // loop level2 to get the data entries
          if (a2[j]) // if nonzero, free actual file data block
            bfree(ip->dev, a2[j]);
        }
        brelse(bp2); // release buffer for level2 block
        bfree(ip->dev, a[i]); // free level2
      }
    }

    brelse(bp); // release buffer for root block
    bfree(ip->dev, ip->addrs[NDIRECT + 1]); // free root
    ip->addrs[NDIRECT + 1] = 0; // clear inode ptr
  }
  ip->size = 0; // set file size = 0, meaning no content
  iupdate(ip); // write updated inode back to disk
  // saves cleared block ptrs and new size of 0, truncation is made perma
}
```

#### Symbolic links with syscall (SYMLINK()) and updating OPEN() to follow links
- handling metadata by creating SYMBOLIC links (indirection, making files point to other files)
- special type of file that does not contain data but a path(string) to another file
- the idea is when opening it, kernel sees it is a link, reads the path inside and opens that TARGET
- task1 is to implement `symlink(target,path)` syscall that creates the path that opens a target
- task2 is to modify `open` syscall to read that path created to reach the target file (follows symlink) UNLESS O_NOFOLLOW is set
##### Steps to consider
1. Define T_SYMLINK in `stat.h` 
	- new file type
```c
#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device
#define T_SYMLINK 4   // Symbolic link (Added)
```

2. Define O_NOFOLLOW
	- new flag that tells open syscall to open `symlink` rather than following shortcut
	- new flag address cannot overlap existing flags
```c
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NOFOLLOW 0x800 (Added) 
```

3. Kernel changes when introducing new syscall
```c
#define SYS_symlink 22 // syscall.h
extern uint64 sys_symlink(void); // syscall.c
[SYS_symlink] sys_symlink, // syscall.c
uint64 sys_symlink(void) // sysfile.c
```

4. User-space changes when introducing new syscall
```c
int symlink(const char *target, const char *path); //user.h
entry("symlink"); //usys.pl
```

5. Implementation of symlink syscall
	- remember to release inode lock and reference, end transaction and return status 0/-1 regardless of error(-1) or success(0)
	- key terms
		- `writei(ip,0,(uint64)target, 0, strlen(target) + 1)`
			- ip = inode writing to = new symlink inode
			- 0 = user_src = source buffer is in kernel mem
			- (uint64)target = address of source buffer, actual string to store
			- next 0 = offset = start writing at beginning of symlink file so target string become contents of symlink inode starting from byte 0
			- strlen(target) + 1 = num of bytes to write
				- +1 because of null terminator 
```c
uint64 // 0 for success, -1 for failure
sys_symlink(void)
{
  // to hold 2 path strings copied from user space
  // target = where symlink points to
  // path = symlink's own path name
  char target[MAXPATH], path[MAXPATH]; // creats 2 char arrays
  struct inode *ip; // ptr to inode that represent new symlink, after create(...), ip point to inode of newly created symlink file

  // fetch target and path from user space
  // gets syscall arg 0 as string and store into target
  // gets syscall arg 1 as string and store into path
  // <0 means -1 and that means failure, so if either fails, return error
  if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    return -1;

  begin_op(); // start filesys operation, transactional

  // create new inode at path, with type SYMLINK
  // does not create target file, creates the link file itself
  ip = create(path, T_SYMLINK, 0, 0); // path = where new symlink file should be
  if (ip == 0) {
    end_op(); // end filesys operation and return failure if cannot create link
    return -1;
  }

  // symlink inode stores target pathname string as file content
  // write target pathname string into symlink inode's data, including null terminator
  // writei returns how many bytes were written thats why there is comparison to check if successfully write entire target string
  if (writei(ip, 0, (uint64)target, 0, strlen(target) + 1) != strlen(target) + 1) {
    // error log
    iunlockput(ip); // releases inode lock and drops reference to inode
    end_op(); // clean up, end filesys transaction
    return -1; // report failure to user space
  }

  iunlockput(ip); // if all succeed, release inode lock and inode reference
  end_op();
  return 0;
}
```

6. Modification to `sys_open` in `sysfile.c`
	- get path and open mode from user space, start fs operation to either create file or find inode
	- if symlink, follow it unless O_NOFOLLOW is set
	- reject invalid cases like writing to directory, allocate fs and fd, set read/write flags and truncate if requested
	- unlock and return fd
```c
uint64
sys_open(void)
{
  char path[MAXPATH]; // pathname from user
  int fd, omode; // declares file descriptor to return to user and open mode flags
  struct file *f; // ptr to kernel file structure
  struct inode *ip; // ptr to inode of opened file, actual filesys object found
  int n; // temp int to store result of argstr
  char target[MAXPATH]; // pathname read from symlink inode
  int depth = 0; // counter for followed symlinks 
  struct inode *next; // ptr to next inode to follow if symlink target resolved

  argint(1, &omode); // get syscall arg1 as int and store in omode (flags)
  // get syscall arg0 as string, store in path (path)
  if((n = argstr(0, path, MAXPATH)) < 0) 
    return -1; // if fail

  begin_op(); // begin transaction

  if(omode & O_CREATE){ // check if user requested file CREATION
    ip = create(path, T_FILE, 0, 0); // if create flag, create regular file inode at path (create file itself, not symlink)
    if(ip == 0){ // if success ip points to new inode so if ip is 0 means fail
      end_op(); // end transaction
      return -1; // fail
    }
  } else {
    // error fallback if cannot resolve pathname to an inode
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip); // if success, lock the inode for safety

    // follow symlinks unless O_NOFOLLOW is set
    while(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)){
      if(depth++ >= 10){ // check if too many symlink hops, limit 10
        iunlockput(ip);  // if exceeded unlock current inode
        end_op(); // and end it, return error
        return -1; // preventing infinite symlink loops
      }
      
      // read symlink's contents into target
      // ip is current symlink node, 0 is mode flag
      // uint64 target is dest buffer in kernel mem
      // 0 is start reading at offset 0
      // MAXPATH is max bytes to read

      if(readi(ip, 0, (uint64)target, 0, MAXPATH) <= 0){
        iunlockput(ip); // if less than 0 = nth to read, unlock inode
        end_op(); // end it, return error
        return -1;
      }
      
      // done with symlink inode, move to next inode
      // release current symlink inode before following to avoid lock issues
      iunlockput(ip);   

      // resolve target path string into another inode
      if((next = namei(target)) == 0){
        end_op();
        return -1;
      }
      ip = next; // move ip to resolved target inode
      // the file to open is target of symlink, not symlink itself
      ilock(ip); // lock new inode
    }
    // end of the symlink-following loop
    // after loop, either ip is real non-symlink inode or 
    // O_NOFOLLOW set so symlink following was skipped
    // or error 

    // check if inode is a directory and user is trying to open with write access
    // directories cannot be opened for writing here, only read
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip); // if invalid directory open, clean up and error
      end_op();
      return -1;
    }
  }
  
  // check if inode is a device file with invalid major number
  // device files use major to identify which device driver handles them
  // if major number is invalid, fail
  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }
  
  //even if inode exist, open cannot succeed unless kernel create open-file object and assign an fd
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f) // if filealloc worked but fdalloc fail
      fileclose(f); // close/free partially created file struct
    iunlockput(ip); // unlock and end
    end_op();
    return -1;
  }

  // check if opened inode is a device
  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE; // initialize file struct as device file
    f->major = ip->major; // for devices, file obj need type and major num (which device driver to use)
  } else { // else treat it as normal inode-backed file
    f->type = FD_INODE; // initialize file struct as normal inode file
    f->off = 0; // file offset to 0 (read/write begins at start of file)
  }
  f->ip = ip; // store inode ptr inside file struct
  f->readable = !(omode & O_WRONLY); // set if file can be read
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR); // set if can be write

  // optional truncation
  // if user requested truncation and inode is a regular file
  // if O_TRUNC is set, empty file contents
  // directories and device files are not truncated here
  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip); // free all file blocks and set size 0
    // erase file's content
  }
  iunlock(ip); // must unlock inode and end transaction
  end_op();
  return fd; // return fd to user = open succeeded
}
```

##### Overview of symbolic links
![[Pasted image 20260317022916.png]]

#### Most common mistakes
1. **Forgot to change both inode structs**  
    `struct inode` and `struct dinode` must match.
    
2. **Did not rebuild `fs.img`**  
    After changing `NDIRECT`, old filesystem image can break things.
    
3. **Forgot `brelse()`**  
    
4. **Did not free double-indirect blocks in `itrunc()`**  
    Then tests may pass partly but leak blocks.
    
5. **Put symlink-following logic in wrong place in `sys_open()`**  
    It should be after `ilock(ip)` and before directory checks.
    
6. **No loop guard for symlinks**  
    Then circular links can hang forever
