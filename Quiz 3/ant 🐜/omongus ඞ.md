# ICT1012 Quiz 3 Predicted Questions & Full Solutions
## Based on Lab3-w8 (File System: Large Files + Symlinks)

---

## Professor's Pattern Analysis (Lab → Quiz Transformation)

### Quiz 1 (derived from Labs w1, w2, w3):
| Lab Task | Quiz Transformation | Type |
|----------|---------------------|------|
| `sixfive` (multiples of 5 or 6) | Changed filter: "sequence of chars 5 and 6, max 3 digits" | **Tweak the condition** |
| `memdump` (format chars i,p,h,c,s,S) | Add new format char `q` (hex-to-ASCII) | **Extend with new case** |
| `hello` syscall + bitwise ops | `swap32`: user-space + kernel syscall endian swap | **Hybrid challenge** |

### Quiz 2 (derived from Labs w3, w5):
| Lab Task | Quiz Transformation | Type |
|----------|---------------------|------|
| `handshake` (1 byte pipe) | Transfer 4 bytes instead of 1, handle truncation/padding | **Scale up existing** |
| `pthread hash table` (add locks) | Given buggy pthread code, fix missing lock/unlock calls | **Debug/fix variant** |
| `uthread` (context switch) | Add `thread_sleep()` and `thread_wakeup()` | **Extend with new feature** |

### Key Patterns:
1. **4-mark questions**: Small tweak to filter/condition, OR extend with one new feature
2. **7-mark challenges**: Combine user-space + kernel, OR add significant new mechanism
3. Professor provides partial/template code with blanks to fill
4. Same codebase, changed requirements
5. Test cases are re-run with different inputs after deadline

---

## TASK 1: Large Files (bmap / itrunc / inode structure)

---

### Question 1A (4 marks): "blockcount" syscall

**Predicted Question:**

> Implement a new system call `int blockcount(char *path)` that counts and prints the number of **allocated** data blocks at each indirection level for a given file path.
>
> The syscall should print:
> ```
> Direct: X
> Singly-indirect: Y
> Doubly-indirect: Z
> Total: W
> ```
>
> Create a user program `user/blockcount.c` that takes a file path as argument and calls the syscall.
>
> Hints:
> - Walk the inode's `addrs[]` array
> - For singly-indirect, `bread()` the indirect block and count non-zero entries
> - For doubly-indirect, walk both levels
> - Remember to `brelse()` every buffer you `bread()`

**Full Solution:**

**Step 1: kernel/syscall.h** (add syscall number)
```c
#define SYS_blockcount 23
```

**Step 2: kernel/syscall.c** (add extern and table entry)
```c
extern uint64 sys_blockcount(void);

// In the syscalls[] array:
[SYS_blockcount] sys_blockcount,

// In the syscall_names[] array (if it exists):
[SYS_blockcount] "blockcount",
```

**Step 3: user/user.h** (add prototype)
```c
int blockcount(const char*);
```

**Step 4: user/usys.pl** (add entry)
```perl
entry("blockcount");
```

**Step 5: kernel/sysfile.c** (implement the syscall)
```c
uint64
sys_blockcount(void)
{
  char path[MAXPATH];
  struct inode *ip;
  int direct_count = 0;
  int single_count = 0;
  int double_count = 0;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Count direct blocks
  for(int i = 0; i < NDIRECT; i++){
    if(ip->addrs[i])
      direct_count++;
  }

  // Count singly-indirect blocks
  if(ip->addrs[NDIRECT]){
    struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
    uint *a = (uint*)bp->data;
    for(int j = 0; j < NINDIRECT; j++){
      if(a[j])
        single_count++;
    }
    brelse(bp);
  }

  // Count doubly-indirect blocks
  if(ip->addrs[NDIRECT+1]){
    struct buf *bp1 = bread(ip->dev, ip->addrs[NDIRECT+1]);
    uint *a1 = (uint*)bp1->data;
    for(int i = 0; i < NINDIRECT; i++){
      if(a1[i]){
        struct buf *bp2 = bread(ip->dev, a1[i]);
        uint *a2 = (uint*)bp2->data;
        for(int j = 0; j < NINDIRECT; j++){
          if(a2[j])
            double_count++;
        }
        brelse(bp2);
      }
    }
    brelse(bp1);
  }

  int total = direct_count + single_count + double_count;
  printf("Direct: %d\n", direct_count);
  printf("Singly-indirect: %d\n", single_count);
  printf("Doubly-indirect: %d\n", double_count);
  printf("Total: %d\n", total);

  iunlockput(ip);
  end_op();
  return total;
}
```

**Step 6: user/blockcount.c** (user program)
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "usage: blockcount <filepath>\n");
    exit(1);
  }
  int result = blockcount(argv[1]);
  if(result < 0){
    fprintf(2, "blockcount: cannot stat %s\n", argv[1]);
    exit(1);
  }
  exit(0);
}
```

**Step 7: Makefile** (add to UPROGS)
```
$U/_blockcount\
```

**Expected output:**
```
$ blockcount big.file
Direct: 11
Singly-indirect: 256
Doubly-indirect: 6313
Total: 6580
```

---

### Question 1B (4 marks): "Two Singly-Indirect Blocks" Layout Change

**Predicted Question:**

> Modify the xv6 file system inode layout so that it uses:
> - 10 direct blocks (NDIRECT = 10)
> - 2 singly-indirect blocks (slots 10 and 11)
> - 1 doubly-indirect block (slot 12)
>
> Total slots: 13 (same as before: addrs[NDIRECT+3])
>
> New MAXFILE = 10 + 256 + 256 + (256 * 256) = 66,058 blocks
>
> Modify `kernel/fs.h`, `kernel/file.h`, `bmap()` in `kernel/fs.c`, and `itrunc()` in `kernel/fs.c`.
>
> Run `bigfile` to verify.

**Full Solution:**

**Step 1: kernel/fs.h**
```c
#define NDIRECT 10
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT + NDINDIRECT)

// On-disk inode structure
struct dinode {
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+3];   // 10 direct + 2 singly-indirect + 1 doubly-indirect
};
```

**Step 2: kernel/file.h** (update in-memory inode)
```c
// In struct inode, change addrs:
uint addrs[NDIRECT+3];
```

**Step 3: kernel/fs.c** (bmap function, replace entirely)
```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  // Direct blocks: 0 .. NDIRECT-1
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  // First singly-indirect block: slot NDIRECT
  if(bn < NINDIRECT){
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;

  // Second singly-indirect block: slot NDIRECT+1
  if(bn < NINDIRECT){
    if((addr = ip->addrs[NDIRECT+1]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT+1] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;

  // Doubly-indirect block: slot NDIRECT+2
  if(bn < NDINDIRECT){
    // Level 1: Master map
    if((addr = ip->addrs[NDIRECT+2]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT+2] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    uint idx1 = bn / NINDIRECT;
    uint idx2 = bn % NINDIRECT;

    // Level 2: Secondary map
    if((addr = a[idx1]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[idx1] = addr;
        log_write(bp);
      }
    }
    brelse(bp);

    if(addr == 0)
      return 0;

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[idx2]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[idx2] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}
```

**Step 4: kernel/fs.c** (itrunc function, replace entirely)
```c
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  // Free direct blocks
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // Free first singly-indirect block
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // Free second singly-indirect block
  if(ip->addrs[NDIRECT+1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }

  // Free doubly-indirect block
  if(ip->addrs[NDIRECT+2]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+2]);
    a = (uint*)bp->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a[i]){
        struct buf *bp2 = bread(ip->dev, a[i]);
        uint *a2 = (uint*)bp2->data;
        for(j = 0; j < NINDIRECT; j++){
          if(a2[j])
            bfree(ip->dev, a2[j]);
        }
        brelse(bp2);
        bfree(ip->dev, a[i]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+2]);
    ip->addrs[NDIRECT+2] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

---

### Question 1C (7 marks): "Triply-Indirect Block" Challenge

**Predicted Question:**

> Extend the xv6 file system to support **triply-indirect** blocks.
>
> New layout:
> - NDIRECT = 10 (slots 0-9)
> - Slot 10: Singly-indirect (256 blocks)
> - Slot 11: Doubly-indirect (256 * 256 = 65,536 blocks)
> - Slot 12: Triply-indirect (256 * 256 * 256 = 16,777,216 blocks)
>
> New MAXFILE = 10 + 256 + 65,536 + 16,777,216 = 16,843,018 blocks
>
> Modify `kernel/fs.h`, `kernel/file.h`, and add triply-indirect support to `bmap()` and `itrunc()` in `kernel/fs.c`.
>
> Note: For testing, `bigfile.c` will be limited to a manageable number of blocks.

**Full Solution:**

**Step 1: kernel/fs.h**
```c
#define NDIRECT 10
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
#define NTINDIRECT (NINDIRECT * NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT + NTINDIRECT)

struct dinode {
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+3];   // 10 direct + 1 single + 1 double + 1 triple
};
```

**Step 2: kernel/file.h**
```c
// In struct inode:
uint addrs[NDIRECT+3];
```

**Step 3: kernel/fs.c** (bmap with triply-indirect support)
```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  // Direct blocks
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  // Singly-indirect
  if(bn < NINDIRECT){
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }
  bn -= NINDIRECT;

  // Doubly-indirect
  if(bn < NDINDIRECT){
    if((addr = ip->addrs[NDIRECT+1]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT+1] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    uint idx1 = bn / NINDIRECT;
    uint idx2 = bn % NINDIRECT;

    if((addr = a[idx1]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[idx1] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    if(addr == 0) return 0;

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[idx2]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[idx2] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }
  bn -= NDINDIRECT;

  // Triply-indirect
  if(bn < NTINDIRECT){
    // Level 1: top-level indirect block
    if((addr = ip->addrs[NDIRECT+2]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT+2] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    uint idx1 = bn / (NINDIRECT * NINDIRECT);
    uint rem = bn % (NINDIRECT * NINDIRECT);
    uint idx2 = rem / NINDIRECT;
    uint idx3 = rem % NINDIRECT;

    // Level 2
    if((addr = a[idx1]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[idx1] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    if(addr == 0) return 0;

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    // Level 3
    if((addr = a[idx2]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[idx2] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    if(addr == 0) return 0;

    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;

    // Level 4: data block
    if((addr = a[idx3]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[idx3] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}
```

**Step 4: kernel/fs.c** (itrunc with triply-indirect cleanup)
```c
void
itrunc(struct inode *ip)
{
  int i, j, k;
  struct buf *bp, *bp2, *bp3;
  uint *a, *a2, *a3;

  // Free direct blocks
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // Free singly-indirect
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // Free doubly-indirect
  if(ip->addrs[NDIRECT+1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a[i]){
        bp2 = bread(ip->dev, a[i]);
        a2 = (uint*)bp2->data;
        for(j = 0; j < NINDIRECT; j++){
          if(a2[j])
            bfree(ip->dev, a2[j]);
        }
        brelse(bp2);
        bfree(ip->dev, a[i]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }

  // Free triply-indirect
  if(ip->addrs[NDIRECT+2]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+2]);
    a = (uint*)bp->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a[i]){
        bp2 = bread(ip->dev, a[i]);
        a2 = (uint*)bp2->data;
        for(j = 0; j < NINDIRECT; j++){
          if(a2[j]){
            bp3 = bread(ip->dev, a2[j]);
            a3 = (uint*)bp3->data;
            for(k = 0; k < NINDIRECT; k++){
              if(a3[k])
                bfree(ip->dev, a3[k]);
            }
            brelse(bp3);
            bfree(ip->dev, a2[j]);
          }
        }
        brelse(bp2);
        bfree(ip->dev, a[i]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+2]);
    ip->addrs[NDIRECT+2] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

---

## TASK 2: Symbolic Links

---

### Question 2A (4 marks): "readlink" syscall

**Predicted Question:**

> Implement a new system call `int readlink(const char *path, char *buf, int bufsize)` that reads the target path stored in a symbolic link WITHOUT following it.
>
> - If `path` is a symlink, copy its target string into `buf` (up to `bufsize` bytes) and return the number of bytes copied.
> - If `path` is not a symlink or does not exist, return -1.
>
> Create a user program `user/readlink.c` that takes a path argument and prints the symlink target.
>
> Expected output:
> ```
> $ symlink /README /mylink
> $ readlink /mylink
> /README
> ```

**Full Solution:**

**Step 1: kernel/syscall.h**
```c
#define SYS_readlink 24
```

**Step 2: kernel/syscall.c**
```c
extern uint64 sys_readlink(void);

// In syscalls[] array:
[SYS_readlink] sys_readlink,
```

**Step 3: user/user.h**
```c
int readlink(const char*, char*, int);
```

**Step 4: user/usys.pl**
```perl
entry("readlink");
```

**Step 5: kernel/sysfile.c** (implement the syscall)
```c
uint64
sys_readlink(void)
{
  char path[MAXPATH];
  char buf[MAXPATH];
  int bufsize;
  struct inode *ip;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;
  if(argint(2, &bufsize) < 0)
    return -1;
  if(bufsize <= 0)
    return -1;

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);

  if(ip->type != T_SYMLINK){
    iunlockput(ip);
    end_op();
    return -1;
  }

  // Read the target path from the symlink's data blocks
  int len = ip->size;
  if(len > bufsize)
    len = bufsize;
  if(len > MAXPATH)
    len = MAXPATH;

  if(readi(ip, 0, (uint64)buf, 0, len) != len){
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();

  // Copy result to user space
  uint64 ubuf;
  argaddr(1, &ubuf);
  struct proc *p = myproc();
  if(copyout(p->pagetable, ubuf, buf, len) < 0)
    return -1;

  return len;
}
```

**Step 6: user/readlink.c** (user program)
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  char buf[512];

  if(argc != 2){
    fprintf(2, "usage: readlink <path>\n");
    exit(1);
  }

  int n = readlink(argv[1], buf, sizeof(buf) - 1);
  if(n < 0){
    fprintf(2, "readlink: %s is not a symlink or does not exist\n", argv[1]);
    exit(1);
  }

  buf[n] = '\0';
  printf("%s\n", buf);
  exit(0);
}
```

**Step 7: Makefile**
```
$U/_readlink\
```

---

### Question 2B (4 marks): "symlinkdepth" syscall

**Predicted Question:**

> Implement a new system call `int symlinkdepth(const char *path)` that returns the number of symlink hops required to reach the final target file.
>
> - If `path` is a regular file or directory (not a symlink), return 0.
> - If `path` is a symlink, follow the chain counting each hop.
> - If a cycle is detected (depth > 10), return -1.
> - If the target does not exist (dangling link), return -2.
>
> Create a user program `user/symlinkdepth.c`.
>
> Expected output:
> ```
> $ symlink /README /link1
> $ symlink /link1 /link2
> $ symlink /link2 /link3
> $ symlinkdepth /README
> Depth: 0
> $ symlinkdepth /link1
> Depth: 1
> $ symlinkdepth /link3
> Depth: 3
> ```

**Full Solution:**

**Step 1: kernel/syscall.h**
```c
#define SYS_symlinkdepth 24
```

**Step 2: kernel/syscall.c**
```c
extern uint64 sys_symlinkdepth(void);

// In syscalls[] array:
[SYS_symlinkdepth] sys_symlinkdepth,
```

**Step 3: user/user.h**
```c
int symlinkdepth(const char*);
```

**Step 4: user/usys.pl**
```perl
entry("symlinkdepth");
```

**Step 5: kernel/sysfile.c**
```c
uint64
sys_symlinkdepth(void)
{
  char path[MAXPATH];
  char target[MAXPATH];
  struct inode *ip;
  int depth = 0;
  int maxdepth = 10;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -2;  // path does not exist
  }

  ilock(ip);

  while(ip->type == T_SYMLINK){
    if(depth >= maxdepth){
      iunlockput(ip);
      end_op();
      return -1;  // cycle detected
    }

    // Read target path from symlink
    int len = ip->size;
    if(len >= MAXPATH)
      len = MAXPATH - 1;
    if(readi(ip, 0, (uint64)target, 0, len) != len){
      iunlockput(ip);
      end_op();
      return -1;
    }
    target[len] = '\0';
    depth++;

    iunlockput(ip);

    // Follow to next inode
    if((ip = namei(target)) == 0){
      end_op();
      return -2;  // dangling link
    }
    ilock(ip);
  }

  iunlockput(ip);
  end_op();
  return depth;
}
```

**Step 6: user/symlinkdepth.c**
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "usage: symlinkdepth <path>\n");
    exit(1);
  }

  int d = symlinkdepth(argv[1]);
  if(d == -1){
    printf("Cycle detected (depth > 10)\n");
  } else if(d == -2){
    printf("Dangling symlink or path not found\n");
  } else {
    printf("Depth: %d\n", d);
  }
  exit(0);
}
```

---

### Question 2C (7 marks): "linkinfo" Challenge - File Metadata Inspector

**Predicted Question:**

> Implement a system call `int linkinfo(const char *path)` and a user program `user/linkinfo.c` that prints detailed metadata about a file path:
>
> For regular files:
> ```
> Type: FILE
> Inode: <inum>
> Links: <nlink>
> Size: <size> bytes
> ```
>
> For directories:
> ```
> Type: DIR
> Inode: <inum>
> Links: <nlink>
> ```
>
> For symbolic links (using O_NOFOLLOW to inspect the link itself):
> ```
> Type: SYMLINK
> Inode: <inum>
> Target: <target_path>
> Chain depth: <depth>
> Final target type: FILE|DIR|DANGLING
> ```
>
> The program must NOT follow the symlink automatically. It must inspect the symlink itself, read its target, then optionally follow the chain to determine the final target type and depth.
>
> Hints:
> - Use `O_NOFOLLOW` when opening symlinks
> - Use `fstat` to get file metadata
> - Walk the symlink chain manually to count depth

**Full Solution:**

**Step 1: kernel/syscall.h**
```c
#define SYS_linkinfo 24
```

**Step 2: kernel/syscall.c**
```c
extern uint64 sys_linkinfo(void);

// In syscalls[] array:
[SYS_linkinfo] sys_linkinfo,
```

**Step 3: user/user.h**
```c
int linkinfo(const char*);
```

**Step 4: user/usys.pl**
```perl
entry("linkinfo");
```

**Step 5: kernel/sysfile.c**
```c
uint64
sys_linkinfo(void)
{
  char path[MAXPATH];
  char target[MAXPATH];
  struct inode *ip;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);

  if(ip->type == T_FILE){
    printf("Type: FILE\n");
    printf("Inode: %d\n", ip->inum);
    printf("Links: %d\n", ip->nlink);
    printf("Size: %d bytes\n", ip->size);
    iunlockput(ip);
    end_op();
    return 0;
  }

  if(ip->type == T_DIR){
    printf("Type: DIR\n");
    printf("Inode: %d\n", ip->inum);
    printf("Links: %d\n", ip->nlink);
    iunlockput(ip);
    end_op();
    return 0;
  }

  if(ip->type == T_SYMLINK){
    printf("Type: SYMLINK\n");
    printf("Inode: %d\n", ip->inum);

    // Read the immediate target
    int len = ip->size;
    if(len >= MAXPATH) len = MAXPATH - 1;
    if(readi(ip, 0, (uint64)target, 0, len) != len){
      iunlockput(ip);
      end_op();
      return -1;
    }
    target[len] = '\0';
    printf("Target: %s\n", target);

    iunlockput(ip);

    // Follow chain to find depth and final type
    int depth = 0;
    int maxdepth = 10;
    struct inode *next;

    while(1){
      depth++;
      next = namei(target);
      if(next == 0){
        printf("Chain depth: %d\n", depth);
        printf("Final target type: DANGLING\n");
        end_op();
        return 0;
      }

      ilock(next);
      if(next->type != T_SYMLINK){
        // Reached final target
        if(next->type == T_FILE)
          printf("Final target type: FILE\n");
        else if(next->type == T_DIR)
          printf("Final target type: DIR\n");
        else
          printf("Final target type: DEVICE\n");
        printf("Chain depth: %d\n", depth);
        iunlockput(next);
        end_op();
        return 0;
      }

      if(depth >= maxdepth){
        printf("Chain depth: CYCLE (>%d)\n", maxdepth);
        printf("Final target type: CYCLE\n");
        iunlockput(next);
        end_op();
        return -1;
      }

      // Read next target
      len = next->size;
      if(len >= MAXPATH) len = MAXPATH - 1;
      if(readi(next, 0, (uint64)target, 0, len) != len){
        iunlockput(next);
        end_op();
        return -1;
      }
      target[len] = '\0';
      iunlockput(next);
    }
  }

  iunlockput(ip);
  end_op();
  return -1;
}
```

**Step 6: user/linkinfo.c**
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "usage: linkinfo <path>\n");
    exit(1);
  }

  int r = linkinfo(argv[1]);
  if(r < 0){
    fprintf(2, "linkinfo: error inspecting %s\n", argv[1]);
    exit(1);
  }
  exit(0);
}
```

**Step 7: Makefile**
```
$U/_linkinfo\
```

**Expected output:**
```
$ linkinfo README
Type: FILE
Inode: 2
Links: 1
Size: 2305 bytes

$ symlink /README /mylink
$ linkinfo /mylink
Type: SYMLINK
Inode: 24
Target: /README
Final target type: FILE
Chain depth: 1

$ symlink /mylink /mylink2
$ linkinfo /mylink2
Type: SYMLINK
Inode: 25
Target: /mylink
Final target type: FILE
Chain depth: 2
```

---

## Summary Table

| # | Task Source | Question | Marks | Transformation Pattern |
|---|-----------|----------|-------|----------------------|
| 1A | Large Files | blockcount syscall | 4 | **New feature on existing code** |
| 1B | Large Files | Two singly-indirect blocks | 4 | **Tweak the configuration** |
| 1C | Large Files | Triply-indirect blocks | 7 | **Deep extension challenge** |
| 2A | Symlinks | readlink syscall | 4 | **New feature on existing code** |
| 2B | Symlinks | symlinkdepth syscall | 4 | **Extend with new measurement** |
| 2C | Symlinks | linkinfo metadata inspector | 7 | **Hybrid challenge** |

---

## Which Questions Are MOST Likely?

Based on the professor's strongest patterns:

1. **VERY LIKELY**: A "tweak the layout" question for Large Files (like 1B). The professor loves changing a number and asking you to cascade the change through bmap/itrunc. This is analogous to how sixfive's filter condition was tweaked.

2. **VERY LIKELY**: A "readlink" or similar inspection syscall for Symlinks (like 2A or 2B). The professor consistently asks "add one more syscall that does X" as a 4-mark extension. Quiz 1 added format 'q' to memdump. Quiz 2 extended handshake bytes. The symlink equivalent is "read the link without following it."

3. **LIKELY**: A challenge combining both tasks or requiring deeper kernel work (like 1C or 2C). Every quiz has had a 7-mark challenge that requires touching multiple kernel files and demonstrating real understanding.
