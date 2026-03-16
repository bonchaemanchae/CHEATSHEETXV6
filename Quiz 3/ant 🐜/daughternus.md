# ICT1012 Operating Systems — Quiz 3 Prep
### Lab3 (Large Files + Symbolic Links) → Predicted Quiz Variants with Full Solutions

---

## How the Professor Converts Labs to Quizzes (Pattern Analysis)

| Lab Task | Quiz Version | Transform Applied |
|---|---|---|
| `sixfive` — print multiples of 5 or 6 | `sixfive` — print sequences of digits 5 and 6, max 3 digits | Changed the filter rule |
| `memdump` — existing format chars | `memdump` — add new format char `q` (hex→ASCII) | Bolt on a new format option |
| `hello` syscall pattern (Lab1-w2) | `swap32` — new syscall doing endian swap | New syscall mirroring existing pattern |
| `handshake` — transfer 1 byte | `handshake` — transfer 4 bytes (word) | Scale the data size |
| `pthread` hash table — add locks | `pthread_locks` — fix broken/missing lock code | Give buggy code, ask for fix |
| `uthread` — basic context switch | `uthread` — add `thread_sleep` / `thread_wakeup` | Extend the thread state machine |

**The four recurring moves:**
1. Tweak a filter rule or format spec
2. Give broken code and ask for the fix
3. Add a new syscall mirroring an existing pattern
4. Extend a data structure, size, or state machine

---

## Summary of All 8 Variants

| ID | Task | Style | Est. Marks |
|---|---|---|---|
| T1-V1 | `fileblocks(fd)` — count allocated blocks via syscall | New syscall (mirrors `hello`) | 4 |
| T1-V2 | Fix buggy `itrunc()` doubly-indirect section | Fix broken code (mirrors `pthread_locks`) | 4 |
| T1-V3 | `blockno(fd, n)` — return physical block number | New syscall exposing `bmap()` | 7 (challenge) |
| T1-V4 | Restructure to dual singly-indirect layout | Structural twist (mirrors `sixfive` rule change) | 7 (challenge) |
| T2-V1 | `readlink(path, buf, n)` — read symlink target | New syscall (mirrors `memdump q`) | 4 |
| T2-V2 | Fix buggy symlink loop in `sys_open()` | Fix broken code (mirrors `pthread_locks`) | 4 |
| T2-V3 | `islink(path)` — check if path is a symlink | Simpler new syscall | 4 |
| T2-V4 | `set_symlink_depth(int)` + per-process depth limit | `monitor_mask` proc-field pattern exactly | 7 (challenge) |

---

---

# TASK 1 — Large Files Variants

---

## T1-V1: `fileblocks(int fd)` Syscall

**Question style:** Add a new syscall `fileblocks(int fd)` that returns the total number of allocated data blocks in the file referred to by `fd`, traversing direct, singly-indirect, and doubly-indirect entries.

**Likelihood: Very high.**

---

### `kernel/syscall.h` — add at the end
```c
#define SYS_fileblocks  25   // use the next available number after SYS_symlink
```

### `kernel/syscall.c` — add extern and table entry
```c
// with the other externs:
extern uint64 sys_fileblocks(void);

// inside syscalls[]:
[SYS_fileblocks] sys_fileblocks,

// inside syscall_names[] (if monitor lab is present):
[SYS_fileblocks] "fileblocks",
```

### `user/user.h` — add prototype
```c
int fileblocks(int);
```

### `user/usys.pl` — add entry
```c
entry("fileblocks");
```

### `kernel/sysfile.c` — add implementation (near `sys_fstat`)
```c
uint64
sys_fileblocks(void)
{
  int fd;
  struct file *f;
  if(argfd(0, &fd, &f) < 0)
    return -1;
  if(f->type != FD_INODE)
    return -1;

  struct inode *ip = f->ip;
  ilock(ip);
  int count = 0;

  // Direct blocks
  for(int i = 0; i < NDIRECT; i++){
    if(ip->addrs[i])
      count++;
  }

  // Singly-indirect blocks
  if(ip->addrs[NDIRECT]){
    struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT]);
    uint *a = (uint*)bp->data;
    for(int j = 0; j < NINDIRECT; j++){
      if(a[j])
        count++;
    }
    brelse(bp);
  }

  // Doubly-indirect blocks
  if(ip->addrs[NDIRECT+1]){
    struct buf *bp1 = bread(ip->dev, ip->addrs[NDIRECT+1]);
    uint *a1 = (uint*)bp1->data;
    for(int i = 0; i < NINDIRECT; i++){
      if(a1[i]){
        struct buf *bp2 = bread(ip->dev, a1[i]);
        uint *a2 = (uint*)bp2->data;
        for(int j = 0; j < NINDIRECT; j++){
          if(a2[j])
            count++;
        }
        brelse(bp2);
      }
    }
    brelse(bp1);
  }

  iunlock(ip);
  return count;
}
```

### `user/fileblocks.c` — test program (add `$U/_fileblocks\` to `UPROGS`)
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "Usage: fileblocks <filename>\n");
    exit(1);
  }
  int fd = open(argv[1], O_RDONLY);
  if(fd < 0){
    fprintf(2, "fileblocks: cannot open %s\n", argv[1]);
    exit(1);
  }
  int n = fileblocks(fd);
  if(n < 0){
    fprintf(2, "fileblocks: syscall failed\n");
    close(fd);
    exit(1);
  }
  printf("%s: %d data blocks allocated\n", argv[1], n);
  close(fd);
  exit(0);
}
```

### Expected output
```
$ fileblocks big.file
big.file: 6580 data blocks allocated
```

---

## T1-V2: Fix Buggy `itrunc()` Doubly-Indirect

**Question style:** The following `itrunc()` implementation for doubly-indirect blocks has bugs. Identify and fix all bugs so that deleting a large file correctly frees all disk blocks.

**Likelihood: Very high.**

---

### Buggy code given in quiz
```c
// BUGGY - in kernel/fs.c, itrunc(), doubly-indirect section:
if(ip->addrs[NDIRECT+1]){
  struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
  uint *a = (uint*)bp->data;
  for(int i = 0; i < NINDIRECT; i++){
    if(a[i]){
      struct buf *bp2 = bread(ip->dev, a[i]);
      uint *b = (uint*)bp2->data;
      for(int j = 0; j < NINDIRECT; j++){
        if(b[j])
          bfree(ip->dev, b[j]);
      }
      // BUG 1: brelse(bp2) missing — buffer cache leak, kernel will panic
      bfree(ip->dev, a[i]);
    }
  }
  // BUG 2: brelse(bp) missing — master map buffer never released
  // BUG 3: ip->addrs[NDIRECT+1] never zeroed — inode still points to freed block
  bfree(ip->dev, ip->addrs[NDIRECT+1]);
}
```

### Fixed version — paste into `kernel/fs.c` inside `itrunc()`
```c
// Doubly-indirect: free all secondary maps and their data blocks
if(ip->addrs[NDIRECT+1]){
  struct buf *bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
  uint *a = (uint*)bp->data;
  for(int i = 0; i < NINDIRECT; i++){
    if(a[i]){
      struct buf *bp2 = bread(ip->dev, a[i]);
      uint *b = (uint*)bp2->data;
      for(int j = 0; j < NINDIRECT; j++){
        if(b[j])
          bfree(ip->dev, b[j]);    // free each data block
      }
      brelse(bp2);                 // FIX 1: release secondary map buffer
      bfree(ip->dev, a[i]);        // free the secondary map block itself
    }
  }
  brelse(bp);                      // FIX 2: release master map buffer
  bfree(ip->dev, ip->addrs[NDIRECT+1]);
  ip->addrs[NDIRECT+1] = 0;        // FIX 3: zero out the inode slot
}
```

No other files need changes. Verify with `make clean && make qemu`, run `bigfile`, then `rm big.file`.

---

## T1-V3: `blockno(int fd, int n)` Syscall

**Question style:** Add a syscall `blockno(int fd, int n)` that returns the physical disk block number of logical block `n` in the file opened by `fd`. Return -1 on error.

**Likelihood: Medium — likely a challenge question.**

---

### `kernel/fs.c` — remove `static` from `bmap`
```c
// Change:
// static uint bmap(struct inode *ip, uint bn)
// To:
uint bmap(struct inode *ip, uint bn)
```

### `kernel/defs.h` — add declaration under the `// fs.c` section
```c
uint        bmap(struct inode*, uint);
```

### `kernel/syscall.h`
```c
#define SYS_blockno  26
```

### `kernel/syscall.c`
```c
extern uint64 sys_blockno(void);
// in syscalls[]:
[SYS_blockno] sys_blockno,
```

### `user/user.h`
```c
int blockno(int, int);
```

### `user/usys.pl`
```c
entry("blockno");
```

### `kernel/sysfile.c`
```c
uint64
sys_blockno(void)
{
  int fd, n;
  struct file *f;
  if(argfd(0, &fd, &f) < 0 || argint(1, &n) < 0)
    return -1;
  if(f->type != FD_INODE || n < 0)
    return -1;

  struct inode *ip = f->ip;
  ilock(ip);

  // Validate n is within the file
  uint fileblocks_max = (ip->size + BSIZE - 1) / BSIZE;
  if((uint)n >= fileblocks_max){
    iunlock(ip);
    return -1;
  }

  uint phys = bmap(ip, (uint)n);
  iunlock(ip);
  return phys;
}
```

### `user/blockno.c` — test program (add `$U/_blockno\` to `UPROGS`)
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: blockno <file> <logical_block_num>\n");
    exit(1);
  }
  int fd = open(argv[1], O_RDONLY);
  if(fd < 0){
    fprintf(2, "blockno: cannot open %s\n", argv[1]);
    exit(1);
  }
  int n = atoi(argv[2]);
  int phys = blockno(fd, n);
  if(phys < 0){
    fprintf(2, "blockno: invalid block number\n");
    close(fd);
    exit(1);
  }
  printf("logical block %d -> physical block %d\n", n, phys);
  close(fd);
  exit(0);
}
```

### Expected output
```
$ blockno big.file 0
logical block 0 -> physical block 71
$ blockno big.file 300
logical block 300 -> physical block 441
```

---

## T1-V4: Restructure to Dual Singly-Indirect Layout

**Question style:** Modify xv6 to use NDIRECT=10 direct blocks, two singly-indirect slots (slots 10 and 11), and one doubly-indirect slot (slot 12). Update `bmap()`, `itrunc()`, and all relevant structures. New MAXFILE = 10 + 256 + 256 + 65536 = 66058.

**Likelihood: Medium — challenge question.**

---

### `kernel/fs.h` — full relevant section
```c
#define NDIRECT 10
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
// Slots: [0..9]=direct, [10]=singly1, [11]=singly2, [12]=doubly
#define MAXFILE (NDIRECT + NINDIRECT + NINDIRECT + NDINDIRECT)

struct dinode {
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+3];   // 10 direct + singly1 + singly2 + doubly
};
```

### `kernel/file.h` — update `struct inode`
```c
uint addrs[NDIRECT+3];
```

### `kernel/param.h`
Ensure `FSSIZE` is `200000` (already done from the lab).

### `kernel/fs.c` — full `bmap()` replacement
```c
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  // Direct blocks [0..NDIRECT-1]
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

  // First singly-indirect: slot[NDIRECT]
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

  // Second singly-indirect: slot[NDIRECT+1]
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

  // Doubly-indirect: slot[NDIRECT+2]
  if(bn < NDINDIRECT){
    if((addr = ip->addrs[NDIRECT+2]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT+2] = addr;
    }
    // Level 1: master map
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
    if(addr == 0)
      return 0;
    // Level 2: secondary map
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

### `kernel/fs.c` — full `itrunc()` replacement
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

  // Free first singly-indirect
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j]) bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  // Free second singly-indirect
  if(ip->addrs[NDIRECT+1]){
    bp = bread(ip->dev, ip->addrs[NDIRECT+1]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j]) bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT+1]);
    ip->addrs[NDIRECT+1] = 0;
  }

  // Free doubly-indirect
  if(ip->addrs[NDIRECT+2]){
    struct buf *bp1 = bread(ip->dev, ip->addrs[NDIRECT+2]);
    uint *a1 = (uint*)bp1->data;
    for(i = 0; i < NINDIRECT; i++){
      if(a1[i]){
        struct buf *bp2 = bread(ip->dev, a1[i]);
        uint *a2 = (uint*)bp2->data;
        for(j = 0; j < NINDIRECT; j++){
          if(a2[j]) bfree(ip->dev, a2[j]);
        }
        brelse(bp2);
        bfree(ip->dev, a1[i]);
      }
    }
    brelse(bp1);
    bfree(ip->dev, ip->addrs[NDIRECT+2]);
    ip->addrs[NDIRECT+2] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}
```

> **Note:** After changing NDIRECT you must `make clean` to regenerate `fs.img`, otherwise mkfs will build an inconsistent image.

---

---

# TASK 2 — Symbolic Links Variants

---

## T2-V1: `readlink(const char*, char*, int)` Syscall

**Question style:** Implement a syscall `readlink(const char *path, char *buf, int bufsiz)` that reads the target path stored inside a symlink into `buf` without following it. Return the number of bytes written, or -1 on error. If the path is not a symlink, return -1.

**Likelihood: Very high.**

---

### `kernel/syscall.h`
```c
#define SYS_readlink  25
```

### `kernel/syscall.c`
```c
extern uint64 sys_readlink(void);
// in syscalls[]:
[SYS_readlink] sys_readlink,
```

### `user/user.h`
```c
int readlink(const char*, char*, int);
```

### `user/usys.pl`
```c
entry("readlink");
```

### `kernel/sysfile.c` — add implementation
```c
uint64
sys_readlink(void)
{
  char path[MAXPATH];
  uint64 buf;
  int bufsiz;
  struct inode *ip;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &buf) < 0 || argint(2, &bufsiz) < 0)
    return -1;
  if(bufsiz <= 0)
    return -1;

  begin_op();
  ip = namei(path);
  if(ip == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  if(ip->type != T_SYMLINK){
    iunlock(ip);
    iput(ip);
    end_op();
    return -1;
  }

  // Read target path from the symlink's data blocks into kernel buffer
  char target[MAXPATH];
  int n = readi(ip, 0, (uint64)target, 0, MAXPATH - 1);
  iunlock(ip);
  iput(ip);
  end_op();

  if(n < 0)
    return -1;
  target[n] = '\0';

  // Copy min(n, bufsiz) bytes to user space
  int copy_n = (n < bufsiz) ? n : bufsiz;
  if(copyout(myproc()->pagetable, buf, target, copy_n) < 0)
    return -1;

  return copy_n;
}
```

### `user/readlink.c` — test program (add `$U/_readlink\` to `UPROGS`)
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "Usage: readlink <symlink_path>\n");
    exit(1);
  }
  char buf[128];
  int n = readlink(argv[1], buf, sizeof(buf) - 1);
  if(n < 0){
    fprintf(2, "readlink: error (not a symlink or path not found)\n");
    exit(1);
  }
  buf[n] = '\0';
  printf("%s -> %s\n", argv[1], buf);
  exit(0);
}
```

### Expected output
```
$ readlink mylink
mylink -> README
```

---

## T2-V2: Fix Buggy Symlink Loop in `sys_open()`

**Question style:** The following symlink-following code in `sys_open()` has three bugs that can cause crashes, memory leaks, or wrong behaviour. Identify and fix all bugs.

**Likelihood: Very high.**

---

### Buggy code given in quiz
```c
// BUGGY - place this inside sys_open() after the initial ilock(ip):
int depth = 0;
while(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)){
  if(depth > 10){                    // BUG 1: should be >= 10, allows 11 hops
    iunlock(ip);
    iput(ip);
    end_op();
    return -1;
  }
  char target[MAXPATH];
  int n = readi(ip, 0, (uint64)target, 0, MAXPATH);
  // BUG 2: no check that readi succeeded (n < 0 means corrupted symlink)
  target[n] = '\0';
  iunlock(ip);
  iput(ip);
  ip = namei(target);
  if(ip == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  depth++;                           // BUG 3: depth incremented AFTER the check,
}                                    // so check at top always sees stale value
```

### Fixed version — paste into `kernel/sysfile.c` inside `sys_open()`, after `ilock(ip)` and before the `if(ip->type == T_DIR ...)` check
```c
// Follow symlinks, up to a maximum depth of 10
int depth = 0;
while(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)){
  if(depth >= 10){                    // FIX 1: >= 10, not > 10
    iunlock(ip);
    iput(ip);
    end_op();
    return -1;
  }
  depth++;                            // FIX 3: increment depth before the hop
  char target[MAXPATH];
  int n = readi(ip, 0, (uint64)target, 0, MAXPATH - 1);
  if(n < 0){                          // FIX 2: check readi return value
    iunlock(ip);
    iput(ip);
    end_op();
    return -1;
  }
  target[n] = '\0';
  iunlock(ip);
  iput(ip);
  ip = namei(target);
  if(ip == 0){
    end_op();
    return -1;
  }
  ilock(ip);
}
```

No other files need changes.

---

## T2-V3: `islink(const char*)` Syscall

**Question style:** Add a syscall `islink(const char *path)` that returns 1 if `path` refers to a symbolic link, 0 if it refers to any other file type, and -1 if the path does not exist. Do not follow the link.

**Likelihood: Medium — likely a 4-mark question.**

---

### `kernel/syscall.h`
```c
#define SYS_islink  25
```

### `kernel/syscall.c`
```c
extern uint64 sys_islink(void);
// in syscalls[]:
[SYS_islink] sys_islink,
```

### `user/user.h`
```c
int islink(const char*);
```

### `user/usys.pl`
```c
entry("islink");
```

### `kernel/sysfile.c`
```c
uint64
sys_islink(void)
{
  char path[MAXPATH];
  struct inode *ip;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  // namei in unmodified xv6 does not follow T_SYMLINK since the kernel only
  // follows them inside sys_open. So namei gives us the raw link inode.
  ip = namei(path);
  if(ip == 0){
    end_op();
    return -1;  // path does not exist
  }
  ilock(ip);
  int result = (ip->type == T_SYMLINK) ? 1 : 0;
  iunlock(ip);
  iput(ip);
  end_op();
  return result;
}
```

### `user/islink.c` — test program (add `$U/_islink\` to `UPROGS`)
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "Usage: islink <path>\n");
    exit(1);
  }
  int r = islink(argv[1]);
  if(r < 0){
    printf("%s: no such file\n", argv[1]);
  } else if(r == 1){
    printf("%s is a symbolic link\n", argv[1]);
  } else {
    printf("%s is not a symbolic link\n", argv[1]);
  }
  exit(0);
}
```

### Expected output
```
$ islink README
README is not a symbolic link
$ islink mylink
mylink is a symbolic link
$ islink nofile
nofile: no such file
```

---

## T2-V4: `set_symlink_depth(int)` Syscall + Per-Process Depth Limit

**Question style:** The current symlink depth limit is hard-coded at 10. Add a syscall `set_symlink_depth(int maxdepth)` that allows a process to configure its own maximum symlink depth (between 1 and 40). Store the limit per-process and apply it in `sys_open()`. Return -1 for invalid values.

**Likelihood: Medium — this is the `monitor_mask` pattern from Lab1-w3 applied to symlinks.**

---

### `kernel/proc.h` — add to `struct proc`
```c
int symlink_maxdepth;   // max symlink hops for this process, default 10
```

### `kernel/proc.c` — in `allocproc()`, after other proc field initialisations
```c
p->symlink_maxdepth = 10;
```

### `kernel/proc.c` — in `fork()`, copy to child (add near `np->sz = p->sz;`)
```c
np->symlink_maxdepth = p->symlink_maxdepth;
```

### `kernel/syscall.h`
```c
#define SYS_set_symlink_depth  25
```

### `kernel/syscall.c`
```c
extern uint64 sys_set_symlink_depth(void);
// in syscalls[]:
[SYS_set_symlink_depth] sys_set_symlink_depth,
```

### `user/user.h`
```c
int set_symlink_depth(int);
```

### `user/usys.pl`
```c
entry("set_symlink_depth");
```

### `kernel/sysproc.c` — implement the syscall
```c
uint64
sys_set_symlink_depth(void)
{
  int maxdepth;
  argint(0, &maxdepth);
  if(maxdepth < 1 || maxdepth > 40)
    return -1;
  myproc()->symlink_maxdepth = maxdepth;
  return 0;
}
```

### `kernel/sysfile.c` — modify the symlink loop in `sys_open()`
```c
// Replace this line inside the symlink-following while loop:
//   if(depth >= 10){
// With:
if(depth >= myproc()->symlink_maxdepth){
```

### `user/symdepth.c` — test program (add `$U/_symdepth\` to `UPROGS`)
```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  if(argc != 2){
    fprintf(2, "Usage: symdepth <maxdepth>\n");
    exit(1);
  }
  int d = atoi(argv[1]);
  if(set_symlink_depth(d) < 0){
    fprintf(2, "symdepth: invalid depth %d (must be 1-40)\n", d);
    exit(1);
  }
  printf("Symlink max depth set to %d\n", d);
  exit(0);
}
```

### Expected output
```
$ symdepth 5
Symlink max depth set to 5
$ symdepth 0
symdepth: invalid depth 0 (must be 1-40)
$ symdepth 41
symdepth: invalid depth 41 (must be 1-40)
```

---

---

# Quick Reference: Files Changed Per Variant

## New Syscall Checklist (applies to T1-V1, T1-V3, T2-V1, T2-V3, T2-V4)

| File | What to add |
|---|---|
| `kernel/syscall.h` | `#define SYS_xxx N` |
| `kernel/syscall.c` | `extern uint64 sys_xxx(void);` + table entry + names entry |
| `user/user.h` | function prototype |
| `user/usys.pl` | `entry("xxx");` |
| `kernel/sysfile.c` or `kernel/sysproc.c` | implementation |
| `user/xxx.c` | test program |
| `Makefile` | `$U/_xxx\` under `UPROGS` |

## Structural Change Checklist (T1-V4)

| File | What changes |
|---|---|
| `kernel/fs.h` | `NDIRECT`, `NDINDIRECT`, `MAXFILE`, `struct dinode.addrs` size |
| `kernel/file.h` | `struct inode.addrs` size |
| `kernel/param.h` | `FSSIZE` (already 200000 from lab) |
| `kernel/fs.c` | entire `bmap()` and `itrunc()` |

## Fix-It Checklist (T1-V2, T2-V2)

Only `kernel/fs.c` (for T1-V2) or `kernel/sysfile.c` (for T2-V2) need changes. No new syscall registration required.
