# ICT1012 Quiz 4 Prep Guide

Predicted alternates and implementation challenges for Quiz 4 (Virtual Memory / Memory Management), based on pattern analysis of Labs 1, 2, 4-w10 and Quizzes 1, 2.

## The professor's mutation playbook

Looking at how `sixfive` became Quiz1's `sixfive` (added "only digits 5 and 6, max 3 digits"), how `memdump` got a new `q` format added, how `handshake` (1 byte) became Quiz2-Q1 `handshake` (4 bytes with padding/truncation rules), and how `uthread` got extended with `thread_sleep`/`thread_wakeup`, the pattern is:

1. **Constraint tightening** — same task, narrower acceptance.
2. **Width/size extension** — byte becomes word, single becomes array, one resource becomes two.
3. **Add a new mode/flag/format option** — extra switch case, extra syscall flag.
4. **Add a sibling syscall** that mirrors but inverts the original.
5. **Bug-injection challenges** — give buggy code, ask student to fix race/deadlock.
6. **Challenge tier** — re-implement a known userland utility as both userspace AND syscall.

The prof never asks for something architecturally new. He asks for the same plumbing you already laid down, with one twist that forces you to actually understand what you wrote.

---

# PART A — Quiz 4 alternates based on Lab4-w10 (mmap/munmap)

Ranked by likelihood given the prof's mutation playbook.

---

## Alternate 1 (HIGHEST likelihood): `mmap` with non-zero offset

**The mutation:** Lab4 explicitly says "assume offset is zero." Quiz forces you to honor offset.

**Task statement (predicted):**
> Extend your `mmap` implementation so that the `offset` argument is honored. `offset` is guaranteed to be page-aligned. The mapped region of length `len` should reflect file bytes `[offset, offset+len)`. Your `munmap` and writeback path must also account for offset (a dirty page at virtual address `va` writes back to file offset `offset + (va - vma->addr)`).

**Files to change:** `kernel/sysfile.c` (sys_mmap), `kernel/proc.c` (mmap_handler, vma_unmap).

**`sys_mmap` — replace the offset extraction and VMA init:**
```c
int offset;
argint(5, &offset);
if (offset < 0 || (offset % PGSIZE) != 0)
    return -1;
// ... after finding free VMA slot ...
p->vmas[i].offset = offset;
```

**`mmap_handler` in `kernel/proc.c` — fix the file read offset:**
```c
int
mmap_handler(uint64 va, struct vma *v)
{
    va = PGROUNDDOWN(va);
    char *mem = kalloc();
    if (mem == 0) return -1;
    memset(mem, 0, PGSIZE);

    // KEY CHANGE: file offset = vma->offset + (va - vma->addr)
    uint64 file_off = v->offset + (va - v->addr);

    ilock(v->f->ip);
    readi(v->f->ip, 0, (uint64)mem, file_off, PGSIZE);
    iunlock(v->f->ip);

    int perm = PTE_U;
    if (v->prot & PROT_READ)  perm |= PTE_R;
    if (v->prot & PROT_WRITE) perm |= PTE_W;

    struct proc *p = myproc();
    if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0) {
        kfree(mem);
        return -1;
    }
    return 0;
}
```

**`vma_unmap` writeback — same fix:**
```c
// inside vma_unmap, when writing dirty page back:
uint64 file_off = v->offset + (a - v->addr);
begin_op();
ilock(v->f->ip);
writei(v->f->ip, 0, pa, file_off, PGSIZE);
iunlock(v->f->ip);
end_op();
```

---

## Alternate 2 (HIGH likelihood): `mremap` — resize an existing mapping

**The mutation:** Same energy as `thread_sleep`/`thread_wakeup` being added as a sibling to `thread_schedule`. New syscall that piggybacks on existing VMA infrastructure.

**Task statement (predicted):**
> Implement `void *mremap(void *old_addr, uint old_len, uint new_len)`. If `new_len < old_len`, shrink the mapping (unmap the tail, write dirty MAP_SHARED pages back). If `new_len > old_len`, grow it in place if the following address range is free; otherwise return `(void*)-1`. The growth region is lazily faulted in like normal mmap pages.

Register the syscall in `kernel/syscall.h`, `kernel/syscall.c`, `user/user.h`, `user/usys.pl` — same drill as Lab 4 mmap.

**`sys_mremap` in `kernel/sysfile.c`:**
```c
uint64
sys_mremap(void)
{
    uint64 old_addr;
    int old_len, new_len;
    argaddr(0, &old_addr);
    argint(1, &old_len);
    argint(2, &new_len);

    if (new_len <= 0 || old_len <= 0) return -1;
    new_len = PGROUNDUP(new_len);
    old_len = PGROUNDUP(old_len);

    struct proc *p = myproc();
    struct vma *v = 0;
    for (int i = 0; i < MAX_VMA; i++) {
        if (p->vmas[i].valid &&
            p->vmas[i].addr == old_addr &&
            p->vmas[i].length == old_len) {
            v = &p->vmas[i];
            break;
        }
    }
    if (v == 0) return -1;

    if (new_len == old_len) return old_addr;

    if (new_len < old_len) {
        // Shrink: unmap the tail
        uint64 unmap_start = old_addr + new_len;
        int unmap_len = old_len - new_len;
        vma_unmap(p, v, unmap_start, unmap_len);
        v->length = new_len;
        sfence_vma();
        return old_addr;
    }

    // Grow: check the next [old_addr+old_len, old_addr+new_len) is free
    uint64 grow_start = old_addr + old_len;
    uint64 grow_end   = old_addr + new_len;
    for (int i = 0; i < MAX_VMA; i++) {
        struct vma *o = &p->vmas[i];
        if (!o->valid || o == v) continue;
        uint64 os = o->addr, oe = o->addr + o->length;
        if (!(grow_end <= os || grow_start >= oe))
            return -1;  // overlap
    }
    v->length = new_len;  // lazy: pages faulted in on first access
    return old_addr;
}
```

**Userspace test:** `user/mremaptest.c`
```c
#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(void) {
    int fd = open("README", O_RDONLY);
    char *p = mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == (char*)-1) { printf("mmap failed\n"); exit(1); }
    printf("first byte: %c\n", p[0]);

    char *q = mremap(p, 4096, 8192);
    if (q == (char*)-1) { printf("mremap grow failed\n"); exit(1); }
    printf("byte at 4096: %c\n", q[4096]);

    char *r = mremap(q, 8192, 4096);
    if (r == (char*)-1) { printf("mremap shrink failed\n"); exit(1); }
    printf("mremap ok\n");
    munmap(r, 4096);
    exit(0);
}
```

---

## Alternate 3 (MEDIUM-HIGH): `MAP_FIXED` flag

**The mutation:** Same energy as Lab1-w1 `memdump` getting a `q` format added in Quiz 1. Add one new flag to an existing syscall and handle it.

**Task statement (predicted):**
> Currently `mmap` ignores the `addr` hint. Add support for the `MAP_FIXED` flag (define it as `0x10` in `kernel/fcntl.h`). When `MAP_FIXED` is set in `flags`, the kernel must use exactly the user-provided `addr` as the mapping base, or fail (return `(void*)-1`) if `addr` is unaligned, zero, conflicts with an existing VMA, or overlaps the heap/stack region.

**`kernel/fcntl.h`:**
```c
#define MAP_FIXED 0x10
```

**Inside `sys_mmap`, before the address-allocation block:**
```c
if (flags & MAP_FIXED) {
    if (addr == 0 || (addr % PGSIZE) != 0) return -1;
    if (addr < p->sz) return -1;            // collides with heap
    if (addr + length >= TRAPFRAME) return -1;
    for (int j = 0; j < MAX_VMA; j++) {
        if (!p->vmas[j].valid) continue;
        uint64 os = p->vmas[j].addr;
        uint64 oe = os + p->vmas[j].length;
        if (!(addr + length <= os || addr >= oe)) return -1;
    }
    p->vmas[i].addr = addr;
    // skip the normal "find a gap" code path
} else {
    // ... existing gap-finding logic ...
}
```

---

## Alternate 4 (MEDIUM): `msync` — explicit flush without unmapping

**The mutation:** Sibling syscall pattern again. You already wrote the dirty-page writeback logic inside `vma_unmap`; expose it directly.

**Task statement (predicted):**
> Implement `int msync(void *addr, uint length)`. For every page in `[addr, addr+length)` that belongs to a `MAP_SHARED` VMA and is dirty (`PTE_D` set), write it back to the underlying file and clear the dirty bit. Pages from `MAP_PRIVATE` mappings are skipped silently. Return 0 on success, -1 if any page in the range is not part of a valid VMA.

**`sys_msync` in `kernel/sysfile.c`:**
```c
uint64
sys_msync(void)
{
    uint64 addr;
    int length;
    argaddr(0, &addr);
    argint(1, &length);

    if ((addr % PGSIZE) != 0) return -1;
    length = PGROUNDUP(length);

    struct proc *p = myproc();
    for (uint64 a = addr; a < addr + length; a += PGSIZE) {
        struct vma *v = 0;
        for (int i = 0; i < MAX_VMA; i++) {
            if (p->vmas[i].valid &&
                a >= p->vmas[i].addr &&
                a <  p->vmas[i].addr + p->vmas[i].length) {
                v = &p->vmas[i];
                break;
            }
        }
        if (v == 0) return -1;
        if (!(v->flags & MAP_SHARED)) continue;

        pte_t *pte = walk(p->pagetable, a, 0);
        if (pte == 0 || (*pte & PTE_V) == 0) continue;
        if ((*pte & PTE_D) == 0) continue;

        uint64 pa = PTE2PA(*pte);
        uint64 file_off = v->offset + (a - v->addr);

        begin_op();
        ilock(v->f->ip);
        writei(v->f->ip, 0, pa, file_off, PGSIZE);
        iunlock(v->f->ip);
        end_op();

        *pte &= ~PTE_D;
    }
    sfence_vma();
    return 0;
}
```

---

## Alternate 5 (LOWER but plausible): Anonymous mmap with `MAP_ANONYMOUS`

**The mutation:** Same shape as Alternate 3 — add a flag — but this one removes the file dependency.

**Task statement (predicted):**
> Add a `MAP_ANONYMOUS` flag (`0x20`). When set, `mmap` ignores `fd` and creates a zero-filled anonymous region. The page-fault handler must zero-fill the page instead of calling `readi`. `munmap` on an anonymous region simply frees pages without writeback. `fork` must duplicate anonymous VMAs without touching any file reference count.

**`kernel/fcntl.h`:** `#define MAP_ANONYMOUS 0x20`

**`sys_mmap` — bypass fd validation and filedup when anonymous:**
```c
struct file *f = 0;
if (!(flags & MAP_ANONYMOUS)) {
    if (argfd(4, 0, &f) < 0) return -1;
    if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && !f->writable) return -1;
    if (!f->readable) return -1;
}
// ... in the VMA init block:
p->vmas[i].f = f;          // null for anonymous
if (f) filedup(f);
```

**`mmap_handler` — zero-fill branch:**
```c
if (v->f == 0) {           // anonymous
    // mem already zeroed by memset above; skip readi
} else {
    uint64 file_off = v->offset + (va - v->addr);
    ilock(v->f->ip);
    readi(v->f->ip, 0, (uint64)mem, file_off, PGSIZE);
    iunlock(v->f->ip);
}
```

**`vma_unmap` writeback path** — guard with `if (v->f && (v->flags & MAP_SHARED))`.
**`fork` and `exit`** — guard `filedup`/`fileclose` with `if (p->vmas[i].f)`.

---

# PART B — Implementation challenges (Quiz 3-style)

Ranked by quiz likelihood for a VM/memory-management quiz.

## Likelihood ranking

1. **Copy-on-write fork** — most likely. It's the canonical xv6 VM lab, and the prof tested user-space utilities (pwd, find) for Quiz 3 from canonical labs.
2. **Lazy/demand page allocation for sbrk** — second most likely. Smaller scope and you literally already wrote the page-fault dispatcher logic in Lab 4.
3. **Page reference counting standalone** — least likely as a standalone question, because it's really a prerequisite for COW.

---

## Implementation 1: Copy-on-Write Fork (MOST LIKELY)

**The task (predicted):**
> Modify xv6 so that `fork()` does not copy the parent's user pages. Instead, both parent and child share the physical pages, and each shared page is marked read-only in both page tables. When either process tries to write, the resulting page fault handler allocates a new copy of the page, marks it writable in the faulting process's page table, and decrements the original page's reference count. Free a physical page only when its reference count drops to zero. `copyout` (used by syscalls writing to user memory) must also trigger COW.

### Step 1: Page reference counting

**`kernel/kalloc.c`** — add ref-count array and modify `kalloc`/`kfree`:

```c
#define PA2IDX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)
#define MAX_PAGES  (PHYSTOP / PGSIZE)

struct {
    struct spinlock lock;
    int count[MAX_PAGES];
} pageref;

void
pageref_init(void)
{
    initlock(&pageref.lock, "pageref");
    for (int i = 0; i < MAX_PAGES; i++) pageref.count[i] = 0;
}

void
pageref_inc(uint64 pa)
{
    acquire(&pageref.lock);
    pageref.count[PA2IDX(pa)]++;
    release(&pageref.lock);
}

int
pageref_dec(uint64 pa)
{
    acquire(&pageref.lock);
    int c = --pageref.count[PA2IDX(pa)];
    release(&pageref.lock);
    return c;
}

int
pageref_get(uint64 pa)
{
    acquire(&pageref.lock);
    int c = pageref.count[PA2IDX(pa)];
    release(&pageref.lock);
    return c;
}
```

Modify `kalloc()`: after `memset`, set ref count to 1.
```c
void *
kalloc(void)
{
    struct run *r;
    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r) kmem.freelist = r->next;
    release(&kmem.lock);
    if (r) {
        memset((char*)r, 5, PGSIZE);
        acquire(&pageref.lock);
        pageref.count[PA2IDX((uint64)r)] = 1;
        release(&pageref.lock);
    }
    return (void*)r;
}
```

Modify `kfree()`: only actually free when ref count hits zero.
```c
void
kfree(void *pa)
{
    struct run *r;
    if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    if (pageref_dec((uint64)pa) > 0) return;  // still referenced

    memset(pa, 1, PGSIZE);
    r = (struct run*)pa;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}
```

Call `pageref_init()` from `kinit()`.

### Step 2: Mark COW pages with a custom PTE bit

**`kernel/riscv.h`:**
```c
#define PTE_COW (1L << 8)   // RISC-V reserves bits 8-9 for software use
```

### Step 3: Rewrite `uvmcopy` in `kernel/vm.c`

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
    pte_t *pte;
    uint64 pa, i;
    uint flags;

    for (i = 0; i < sz; i += PGSIZE) {
        if ((pte = walk(old, i, 0)) == 0)
            panic("uvmcopy: pte should exist");
        if ((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");

        // Strip write, mark COW, in BOTH parent and child PTEs
        if (*pte & PTE_W) {
            *pte &= ~PTE_W;
            *pte |=  PTE_COW;
        }

        pa    = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);

        if (mappages(new, i, PGSIZE, pa, flags) != 0)
            goto err;

        pageref_inc(pa);
    }
    return 0;

err:
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
}
```

### Step 4: COW fault handler

**`kernel/vm.c`** — add this function:
```c
int
cow_handler(pagetable_t pagetable, uint64 va)
{
    if (va >= MAXVA) return -1;
    va = PGROUNDDOWN(va);

    pte_t *pte = walk(pagetable, va, 0);
    if (pte == 0) return -1;
    if ((*pte & PTE_V) == 0) return -1;
    if ((*pte & PTE_U) == 0) return -1;
    if ((*pte & PTE_COW) == 0) return -1;

    uint64 pa = PTE2PA(*pte);
    uint flags = PTE_FLAGS(*pte);

    // Optimization: if we're the only reference, just promote in place
    if (pageref_get(pa) == 1) {
        *pte |=  PTE_W;
        *pte &= ~PTE_COW;
        return 0;
    }

    char *mem = kalloc();
    if (mem == 0) return -1;
    memmove(mem, (void*)pa, PGSIZE);

    flags |=  PTE_W;
    flags &= ~PTE_COW;

    *pte = PA2PTE((uint64)mem) | flags;
    kfree((void*)pa);  // decrements ref on the old page
    return 0;
}
```

### Step 5: Wire it into `usertrap`

**`kernel/trap.c`** in `usertrap()`, in the syscall/devintr/else chain, add a branch for `r_scause() == 15` (store page fault):
```c
} else if (r_scause() == 15) {
    uint64 va = r_stval();
    if (cow_handler(p->pagetable, va) < 0) {
        printf("usertrap: COW failed pid=%d va=%p\n", p->pid, va);
        setkilled(p);
    }
}
```

### Step 6: Fix `copyout` (kernel writes to user memory)

Without this, syscalls like `read` that write into user buffers will silently corrupt shared pages. **`kernel/vm.c`** in `copyout()`, before each `memmove`, check the destination PTE:
```c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
    uint64 n, va0, pa0;
    while (len > 0) {
        va0 = PGROUNDDOWN(dstva);
        pte_t *pte = walk(pagetable, va0, 0);
        if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
            return -1;
        if (*pte & PTE_COW) {
            if (cow_handler(pagetable, va0) < 0) return -1;
        }
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0) return -1;
        n = PGSIZE - (dstva - va0);
        if (n > len) n = len;
        memmove((void*)(pa0 + (dstva - va0)), src, n);
        len  -= n;
        src  += n;
        dstva = va0 + n;
    }
    return 0;
}
```

### Step 7: User test `user/cowtest.c`

```c
#include "kernel/types.h"
#include "user/user.h"

int main(void) {
    char *buf = sbrk(4096 * 4);
    for (int i = 0; i < 4096*4; i++) buf[i] = 'A';

    int pid = fork();
    if (pid == 0) {
        for (int i = 0; i < 4096*4; i++) buf[i] = 'B';
        for (int i = 0; i < 4096*4; i++)
            if (buf[i] != 'B') { printf("child corrupt\n"); exit(1); }
        printf("child ok\n");
        exit(0);
    }
    wait(0);
    for (int i = 0; i < 4096*4; i++)
        if (buf[i] != 'A') { printf("parent corrupt at %d\n", i); exit(1); }
    printf("parent ok\n");
    exit(0);
}
```

Add `$U/_cowtest` to UPROGS. Expected: both `child ok` and `parent ok`.

---

## Implementation 2: Lazy `sbrk` (Demand Paging)

**The task (predicted):**
> Modify `sbrk(n)` so that when `n > 0`, it only increases `p->sz` and does NOT allocate physical memory. Allocation happens lazily in the page fault handler when the new memory is first touched. When `n < 0`, free immediately as today. The kernel must also handle the case where a syscall passes a not-yet-allocated lazy address through `copyin`/`copyout`.

### Step 1: Make `sys_sbrk` lazy

**`kernel/sysproc.c`:**
```c
uint64
sys_sbrk(void)
{
    int n;
    uint64 addr;
    struct proc *p = myproc();

    argint(0, &n);
    addr = p->sz;

    if (n < 0) {
        if (p->sz + n < 0) return -1;
        uvmdealloc(p->pagetable, p->sz, p->sz + n);
        p->sz += n;
    } else {
        // Lazy: just bump p->sz, do not allocate
        if (p->sz + n >= MAXVA) return -1;
        p->sz += n;
    }
    return addr;
}
```

### Step 2: Lazy fault handler

**`kernel/vm.c`:**
```c
int
lazy_handler(uint64 va)
{
    struct proc *p = myproc();
    if (va >= p->sz) return -1;
    if (va < PGROUNDUP(p->trapframe->sp)) return -1;  // guard page

    va = PGROUNDDOWN(va);
    char *mem = kalloc();
    if (mem == 0) return -1;
    memset(mem, 0, PGSIZE);
    if (mappages(p->pagetable, va, PGSIZE,
                 (uint64)mem, PTE_R|PTE_W|PTE_U) != 0) {
        kfree(mem);
        return -1;
    }
    return 0;
}
```

### Step 3: Wire into `usertrap` for scause 13 and 15

**`kernel/trap.c`:**
```c
} else if (r_scause() == 13 || r_scause() == 15) {
    uint64 va = r_stval();
    if (lazy_handler(va) < 0) {
        printf("usertrap: lazy alloc failed pid=%d va=%p\n", p->pid, va);
        setkilled(p);
    }
}
```

### Step 4: Make `uvmunmap` tolerant of lazy holes

Lazy pages aren't mapped yet, so unmapping a range that contains them must not panic. **`kernel/vm.c`** in `uvmunmap`, replace the panics:
```c
for (a = va; a < va + npages*PGSIZE; a += PGSIZE) {
    if ((pte = walk(pagetable, a, 0)) == 0) continue;       // was panic
    if ((*pte & PTE_V) == 0)               continue;       // was panic
    if (PTE_FLAGS(*pte) == PTE_V)
        panic("uvmunmap: not a leaf");
    if (do_free) {
        uint64 pa = PTE2PA(*pte);
        kfree((void*)pa);
    }
    *pte = 0;
}
```

Same treatment for `uvmcopy` (skip not-yet-allocated pages instead of panicking) so `fork` works after a lazy `sbrk`.

### Step 5: Fix `walkaddr` so `copyin`/`copyout` trigger lazy allocation

**`kernel/vm.c`:**
```c
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;
    uint64 pa;
    struct proc *p = myproc();

    if (va >= MAXVA) return 0;

    pte = walk(pagetable, va, 0);
    if (pte == 0 || (*pte & PTE_V) == 0) {
        // possibly lazy
        if (p && va < p->sz && va >= PGROUNDUP(p->trapframe->sp)) {
            if (lazy_handler(va) < 0) return 0;
            pte = walk(pagetable, va, 0);
            if (pte == 0) return 0;
        } else {
            return 0;
        }
    }
    if ((*pte & PTE_U) == 0) return 0;
    pa = PTE2PA(*pte);
    return pa;
}
```

### Step 6: User test `user/lazytest.c`

```c
#include "kernel/types.h"
#include "user/user.h"

int main(void) {
    char *p = sbrk(1024 * 1024 * 32);   // 32 MB, instant on lazy xv6
    printf("sbrk returned %p\n", p);
    p[0] = 'X';
    p[4096] = 'Y';
    p[1024*1024*16] = 'Z';
    printf("lazy ok: %c %c %c\n", p[0], p[4096], p[1024*1024*16]);
    exit(0);
}
```

On unmodified xv6, the `sbrk(32MB)` call eagerly allocates 32MB of physical pages. On your lazy version it returns instantly and pages get faulted in only on the three writes.

---

## Implementation 3: Page Reference Counting (standalone)

You already have this code in Implementation 1, Step 1. As a standalone question the prof would phrase it as:

> Add a per-physical-page reference counter to xv6's page allocator. Modify `kalloc` to set it to 1, add helper functions `pageref_inc` and `pageref_dec`, and modify `kfree` so that a page is only actually freed when its reference count reaches zero. Write a kernel test that calls `kalloc`, increments the count manually, calls `kfree` twice, and verifies the page is only on the free list after the second `kfree`.

Use the exact code from Implementation 1, Step 1.

---

# Brutal honesty before you walk into the quiz

Three things you should not let yourself off the hook on:

**One.** This is pattern-matching from the prof's previous quizzes. He could absolutely throw a curveball that mirrors none of these. The Quiz 3 `pwd`/`find` precedent suggests he sometimes pulls from totally outside the lab PDF. For VM, the canonical "outside" picks beyond what's listed are: mapping the kernel's own page table per-process and superpages. If you have prep time left after these, glance at those too.

**Two.** Every COW solution on the internet, including the one above, is subtly wrong on edge cases the autograder loves: forking a process whose stack page is COW'd, then the child immediately fork-bombs; `copyout` into a page that's COW and unmapped lazy at the same time; freeing the last reference while another CPU is mid-`memmove`. If COW shows up, expect at least one test case that hits one of these. The optimization in `cow_handler` where it promotes in place if `pageref_get == 1` has a TOCTOU race that the lock inside `pageref_get` does not actually fix, because another CPU can `pageref_inc` between the check and the write to the PTE. For a quiz under time pressure that race almost never trips, but know it's there.

**Three.** Do not paste any of this without compiling it once on your own xv6 tree first. It was written in a chat window without `make qemu` to back it up. The shapes are right, the integration points are right, but a missing semicolon or a stale function signature will eat 10 minutes of your quiz time. Compile each one tonight in a scratch branch so the muscle memory is there.
