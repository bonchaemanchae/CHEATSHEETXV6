# ICT1012 Operating Systems -- Quiz 3 Predictions (Based on Lab1, Lab2, Quiz1 Pattern)

## How the Professor Converts Labs → Quiz Questions

1.  **Same program, different condition**
    -   Example: `sixfive`
    -   Lab: print numbers multiple of 5 or 6
    -   Quiz: print numbers made only of digits 5 and 6
2.  **Extend existing functionality**
    -   Example: `memdump`
    -   Quiz added new format `q`.
3.  **User-space logic → Kernel syscall**
    -   Example: endian swap in Quiz1 challenge.
4.  **Large lab simplified into one focused change**
    -   Only one part of the system is tested.

------------------------------------------------------------------------

# Predicted Quiz3 Questions

## Task 1 -- Large Files

### Q1 blockcount -- count file blocks

``` c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[])
{
    struct stat st;

    if(argc != 2){
        fprintf(2, "usage: blockcount <file>\n");
        exit(1);
    }

    if(stat(argv[1], &st) < 0){
        printf("cannot stat %s\n", argv[1]);
        exit(1);
    }

    int blocks = (st.size + BSIZE - 1) / BSIZE;

    printf("%d\n", blocks);

    exit(0);
}
```

------------------------------------------------------------------------

### Q2 detect block type inside `bmap()`

``` c
if(bn < NDIRECT){
    printf("direct block\n");
}
else if(bn < NDIRECT + NINDIRECT){
    printf("indirect block\n");
}
else{
    printf("double indirect block\n");
}
```

------------------------------------------------------------------------

### Q3 limit file size

``` c
#define MAXFILEBLOCKS 5000

if(bn >= MAXFILEBLOCKS)
    panic("file too large");
```

------------------------------------------------------------------------

### Q4 filesize command

``` c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    struct stat st;

    if(argc != 2){
        fprintf(2,"usage: filesize <file>\n");
        exit(1);
    }

    if(stat(argv[1], &st) < 0){
        printf("cannot stat\n");
        exit(1);
    }

    printf("%d KB\n", st.size / 1024);

    exit(0);
}
```

------------------------------------------------------------------------

# Task 2 -- Symbolic Links

### Q5 readlink

``` c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    char buf[128];
    int fd;

    if(argc != 2){
        fprintf(2,"usage: readlink <link>\n");
        exit(1);
    }

    fd = open(argv[1], O_NOFOLLOW);

    if(fd < 0){
        printf("cannot open link\n");
        exit(1);
    }

    int n = read(fd, buf, sizeof(buf));

    if(n > 0){
        buf[n] = 0;
        printf("%s\n", buf);
    }

    close(fd);
    exit(0);
}
```

------------------------------------------------------------------------

### Q6 limit symlink traversal

``` c
int depth = 0;

while(ip->type == T_SYMLINK && !(omode & O_NOFOLLOW)){

    if(depth++ > 5){
        iunlockput(ip);
        return -1;
    }

    char path[MAXPATH];

    readi(ip, 0, (uint64)path, 0, MAXPATH);

    iunlockput(ip);

    if((ip = namei(path)) == 0)
        return -1;

    ilock(ip);
}
```

------------------------------------------------------------------------

### Q7 issymlink

``` c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    struct stat st;

    if(argc != 2){
        fprintf(2,"usage: issymlink <file>\n");
        exit(1);
    }

    if(stat(argv[1], &st) < 0){
        printf("error\n");
        exit(1);
    }

    if(st.type == T_SYMLINK)
        printf("yes\n");
    else
        printf("no\n");

    exit(0);
}
```

------------------------------------------------------------------------

### Q8 symlinkcount

``` c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    char path[128];
    int depth = 0;

    if(argc != 2){
        fprintf(2,"usage: symlinkcount <file>\n");
        exit(1);
    }

    strcpy(path, argv[1]);

    while(1){

        int fd = open(path, O_NOFOLLOW);

        if(fd < 0)
            break;

        struct stat st;
        fstat(fd, &st);

        if(st.type != T_SYMLINK){
            close(fd);
            break;
        }

        int n = read(fd, path, sizeof(path));

        path[n] = 0;

        depth++;

        close(fd);
    }

    printf("%d\n", depth);

    exit(0);
}
```

------------------------------------------------------------------------

# High‑Probability Quiz Questions (Most Likely)

These are extremely common xv6 quiz questions:

1.  Modify `sys_open()` to limit symlink depth.
2.  Add a new flag similar to `O_NOFOLLOW`.
3.  Modify `bmap()` logic.
4.  Write a small user program that inspects file metadata.
