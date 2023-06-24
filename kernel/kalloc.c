// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  uint refs[MAXPAGES];
} kref;


void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kref.lock, "kref");
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

void
krefset(void *pa,uint64 refsnum)
{
  uint64 indx = FRAMEINDX(pa,end);
  
  if(refsnum > NPROC)
    panic("krefset");

  acquire(&kref.lock);
  kref.refs[indx] = refsnum;
  release(&kref.lock);
}

uint
krefinc(void *pa)
{
  uint ret;
  uint64 indx = FRAMEINDX(pa,end);
  
  acquire(&kref.lock);

  if(kref.refs > NPROC){
    release(&kref.lock);
    panic("krefinc");
  }
  
  ret = ++kref.refs[indx] ;
  
  releakrefsetse(&kref.lock);
}

uint
krefdec(void *pa)
{
  uint ret;
  uint64 indx = FRAMEINDX(pa,end);

  acquire(&kref.lock);
  
  ret = (kref.refs > 0) ? --kref.refs[indx] : kref.refs[indx];
  
  release(&kref.lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // skip if there is another reference
  if(krefdec(pa) > 0)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

uint
infofreeram()
{
  int pgcount = 0;

  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  while(r){
    pgcount++;
    r = r->next;
  }
  release(&kmem.lock);
  
  return pgcount * PGSIZE;
}