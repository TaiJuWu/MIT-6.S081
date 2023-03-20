// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define NUM_OF_PAGE ((PHYSTOP - KERNBASE) / PGSIZE)
#define PA2IDX(pa)  (((uint64)pa - KERNBASE) / PGSIZE)

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int ref_counter[NUM_OF_PAGE];
  struct spinlock ref_lock[NUM_OF_PAGE];
} kmem;

void
kinit()
{
  for(int i = 0; i < NUM_OF_PAGE; ++i) {
    initlock(&kmem.ref_lock[i], "kmem.ref_lock");
  }
  memset(kmem.ref_counter, 0, sizeof(kmem.ref_counter));
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  uint64 idx;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  r = (struct run*)pa;
  idx = PA2IDX((uint64)r);

  acquire(&kmem.ref_lock[idx]);
  kmem.ref_counter[idx] -= 1;
  if(kmem.ref_counter[idx] <= 0) {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    kmem.ref_counter[idx] = 0;
    release(&kmem.lock);
  }
  release(&kmem.ref_lock[idx]);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  uint64 idx;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    idx = PA2IDX((uint64)r);
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&kmem.ref_lock[idx]);
    kmem.ref_counter[idx] = 1;
    release(&kmem.ref_lock[idx]);
  }
    
  return (void*)r;
}

void increase_ref_counter(void *pa) {
  uint64 idx = PA2IDX(pa);
  acquire(&kmem.ref_lock[idx]);
  kmem.ref_counter[idx] += 1;
  release(&kmem.ref_lock[idx]);
}