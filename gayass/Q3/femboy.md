
## 1) Relative symlink targets

### Twist

Instead of only absolute targets like `/a/b/file`, allow:

- absolute: `/x/y`
    
- relative: `../x/y` or `file.txt`
    
### What changes

Your lab-style `sys_open()` does:

next = namei(target);

That only works nicely for absolute paths.

### Correct idea

If the target is relative, resolve it relative to the **directory containing the symlink**, not the caller’s current working directory.

### Solution shape

When you encounter a symlink inode:

1. keep track of the pathname used to reach that symlink
    
2. split it into:
    
    - parent directory path
        
    - symlink name
        
3. if `target[0] == '/'`, use `namei(target)`
    
4. otherwise build:
    

resolved = parent_dir + "/" + target

then call `namei(resolved)`.

### Sketch

char resolved[MAXPATH];  
if(target[0] == '/'){  
  safestrcpy(resolved, target, MAXPATH);  
} else {  
  // assume linkpath contains path used to reach current symlink  
  dirname(linkpath, resolved);         // e.g. "/a/b"  
  appendslash(resolved);               // "/a/b/"  
  safestrcat(resolved, target, MAXPATH); // "/a/b/../c"  
}  
next = namei(resolved);

### Hidden edge case

Need path normalization eventually, but for quiz level they may accept simple concatenation if `namei()` can still walk `.` and `..`.

---

## 2) `readlink()` syscall

### Twist

Instead of just creating and following symlinks, add:

int readlink(const char *path, char *buf, int n);

### What it should do

- look up `path`
    
- make sure it is a symlink
    
- copy the stored target string into `buf`
    
- do **not** follow the link
    

### Kernel solution

Add syscall registration, then:

uint64  
sys_readlink(void)  
{  
  char path[MAXPATH];  
  uint64 ubuf;  
  int n;  
  struct inode *ip;  
  int r;  
  
  if(argstr(0, path, MAXPATH) < 0)  
    return -1;  
  if(argaddr(1, &ubuf) < 0)  
    return -1;  
  if(argint(2, &n) < 0)  
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
  
  r = readi(ip, 0, ubuf, 0, n);  
  iunlockput(ip);  
  end_op();  
  return r;  
}

### Key point

Unlike `open()`, this should **not** traverse the link.

---

## 3) Partial truncate, not full `itrunc()`

### Twist

Instead of truncating to size 0, truncate a file to size `newsize`.

### What makes it harder

You must free only blocks **after** the new logical EOF.

### Correct idea

Compute:

- old last block
    
- new last block
    

Free all blocks strictly beyond the new end.

### Practical quiz strategy

They probably would not expect a perfect generic solution from scratch under time pressure. The safer structure is:

1. `old_nblocks = ceil(ip->size / BSIZE)`
    
2. `new_nblocks = ceil(newsize / BSIZE)`
    
3. for each file block number `bn` from `new_nblocks` to `old_nblocks-1`:
    
    - free that data block if allocated
        
4. if entire indirect or double-indirect index blocks become empty, free those too
    
5. set `ip->size = newsize`
    
6. `iupdate(ip)`
    

### Pseudocode

for(bn = new_nblocks; bn < old_nblocks; bn++){  
  bfree_file_block(ip, bn);   // helper that handles direct/single/double cases  
}  
ip->size = newsize;  
iupdate(ip);

### Exam trick

If they ask this, the real test is whether you understand that `itrunc()` is **bottom-up reverse mapping**, not just “set size to 0”.

---

## 4) Sparse-file aware mapping

### Twist

Reads of holes should return zeros, but blocks should only be allocated on write.

### Why current `bmap()` is not enough

Your lab `bmap()` allocates on demand. That is fine for writes, but a sparse read should not create blocks.

### Clean solution

Split mapping into two helpers:

uint bmap_lookup(struct inode *ip, uint bn);   // no allocation  
uint bmap_alloc(struct inode *ip, uint bn);    // allocate if missing

### `bmap_lookup()`

Same navigation through direct/single/double, but:

- if any pointer is missing, return 0
    
- do not call `balloc()`
    

### `bmap_alloc()`

This is your normal lab `bmap()`.

### Read path

In `readi()`:

- call `bmap_lookup()`
    
- if result is 0, memset destination bytes to 0 for that chunk
    

### Write path

In `writei()`:

- call `bmap_alloc()`
    

This is a very believable “harder than lab” twist because it tests whether you know allocation and lookup are logically different.

---

## 5) Triple-indirect blocks

### Twist

Go one level deeper than the lab.

### Constants

#define NDIRECT 11  
#define NINDIRECT (BSIZE / sizeof(uint))  
#define NDINDIRECT (NINDIRECT * NINDIRECT)  
#define NTINDIRECT (NINDIRECT * NINDIRECT * NINDIRECT)  
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT + NTINDIRECT)

### Inode slots

Now you need:

addrs[NDIRECT + 3]

### Index math

After subtracting direct and single and double:

int i1 = bn / NDINDIRECT;  
int rem = bn % NDINDIRECT;  
int i2 = rem / NINDIRECT;  
int i3 = rem % NINDIRECT;

### Path

- inode → triple root
    
- triple root entry `i1` → double block
    
- double block entry `i2` → single block
    
- single block entry `i3` → data block
    

### `itrunc()`

Add one more nested loop:

- free data blocks
    
- free single blocks
    
- free double blocks
    
- free triple root
    

### Real quiz takeaway

If they do this, do not panic. It is just the same tree pattern one level deeper.

---

## 6) Boundary bug in `bmap()`

### Twist

They give almost-correct code and hidden tests fail near:

- first single-indirect block
    
- first double-indirect block
    
- `MAXFILE`
    

### Typical bug

Forgetting:

bn -= NDIRECT;  
...  
bn -= NINDIRECT;

or subtracting in the wrong place.

### Correct logic

if(bn < NDIRECT) ...  
bn -= NDIRECT;  
  
if(bn < NINDIRECT) ...  
bn -= NINDIRECT;  
  
if(bn < NDINDIRECT) ...  
panic("bmap: out of range");

### Why

Each subtraction converts from:

- global file block numbering  
    to
    
- index within the next region
    

### Another common bug

Not checking `balloc()` failure in double-indirect code.

Safer form:

if((addr = ip->addrs[NDIRECT+1]) == 0){  
  addr = balloc(ip->dev);  
  if(addr == 0)  
    return 0;  
  ip->addrs[NDIRECT+1] = addr;  
}

Same for level-2 and final data block allocation.

---

## 7) Symlink loops with stricter hidden tests

### Twist

The visible tests may only check simple cycles, but hidden tests may include:

- `a -> a`
    
- `a -> b -> a`
    
- long chain longer than 10
    
- broken link in middle of chain
    

The lab already highlights chains, concurrent links, and cycles as core cases.

### Correct solution

Your `while` loop must:

- increment depth every time it follows a link
    
- stop after a fixed bound
    
- return error if `namei(target)` fails
    

### Robust loop

while(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)){  
  if(depth++ >= 10){  
    iunlockput(ip);  
    end_op();  
    return -1;  
  }  
  
  if(readi(ip, 0, (uint64)target, 0, MAXPATH) <= 0){  
    iunlockput(ip);  
    end_op();  
    return -1;  
  }  
  
  target[MAXPATH-1] = '\0';  
  iunlockput(ip);  
  
  if((next = namei(target)) == 0){  
    end_op();  
    return -1;  
  }  
  
  ip = next;  
  ilock(ip);  
}

### Why hidden tests fail otherwise

- no depth counter → infinite loop
    
- no null termination → garbage path
    
- no `namei()` failure check → crash on dangling link
    

---

## 8) Locking bug in symlink-following

### Twist

They give code that does this:

next = namei(target);  
ip = next;  
ilock(ip);  
iunlockput(old_ip);

### Why it is bad

You kept the old inode locked while doing pathname lookup. That can deadlock or fail concurrency tests.

The lab’s symlink tests explicitly include concurrent opens/creates, so lock discipline matters.

### Correct order

iunlockput(ip);  
if((next = namei(target)) == 0){  
  end_op();  
  return -1;  
}  
ip = next;  
ilock(ip);

### Memory rule

**Release current symlink inode before resolving the next target.**

---

## 9) `open()` follows symlinks only for regular open, not for metadata open

### Twist

A new flag like `O_META` or reuse `O_NOFOLLOW`:

- if `O_NOFOLLOW`, open the symlink inode itself
    
- otherwise follow target
    

### Correct handling

Your current loop already supports this:

while(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW))

### Hidden edge case

If `O_NOFOLLOW | O_TRUNC` is passed on a symlink:

- do **not** follow
    
- do not truncate the target
    
- only act on symlink inode if allowed by spec
    

### Safer check

if((omode & O_TRUNC) && ip->type == T_FILE)  
  itrunc(ip);

That way only regular files get truncated.

---

## 10) Broken symlink handling

### Twist

What should happen if the target no longer exists?

The lab notes that a symlink can point to a deleted file, so open should fail if resolution fails.

### Correct behavior

This is enough:

if((next = namei(target)) == 0){  
  end_op();  
  return -1;  
}

### Do not

- silently create target
    
- return the symlink inode unless `O_NOFOLLOW`
    
- keep using old `ip`
    

---

## 11) “Keep 12 direct blocks and also add double-indirect”

### Twist

This is a trick design question.

### Reality

If the on-disk inode format has fixed size, keeping all direct slots and also adding another slot usually means:

- enlarge inode layout
    
- update `struct dinode`
    
- update `struct inode`
    
- rebuild filesystem image
    

The lab emphasizes that `struct inode` and `struct dinode` must match and that `fs.img` must be rebuilt if inode layout changes.

### Best quiz answer

Say:

- not possible with the old on-disk inode size unless layout changes
    
- if allowed to change layout, use `addrs[NDIRECT+2]` with larger inode format and rebuild `fs.img`
    

That is probably what they want to hear.

---

## 12) Harder `itrunc()` bug: freeing index block before children

### Twist

They may show wrong code like:

bfree(ip->dev, ip->addrs[NDIRECT+1]);  
bp = bread(ip->dev, ip->addrs[NDIRECT+1]);

### Why wrong

You freed the root before using it to find child blocks.

### Correct order

For double-indirect:

1. read root block
    
2. for each level-2 block:
    
    - read level-2 block
        
    - free all data blocks
        
    - free level-2 block
        
3. free root block
    
4. clear inode pointer
    

This is exactly the bottom-up cleanup pattern from the lab.