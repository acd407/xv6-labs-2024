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
  int refcount[(PHYSTOP - KERNBASE)/PGSIZE];
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

// 获取物理地址对应的引用计数数组索引
static int
pa2index(uint64 pa)
{
  if (pa < (uint64)end)
    return -1;
  uint64 idx = (pa - (uint64)end) / PGSIZE;
  if (idx >= (PHYSTOP - KERNBASE)/PGSIZE)
    return -1;
  return idx;
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kmem.refcount[pa2index((uint64)p)] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa) // 相当于带内存释放的 dec_ref
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  // dec_ref 被取缔了，要用就用 kfree
  // 只减少引用计数，很容易造成错误，产生内存泄露
  int count = --kmem.refcount[pa2index((uint64)pa)];
  if(count == 0) {
    memset(pa, 1, PGSIZE);
    r = (struct run *)pa;
    r->next = kmem.freelist;
    kmem.freelist = r; 
  }
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

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    kmem.refcount[pa2index((uint64)r)] = 1;
  }
  return (void*)r;
}

//增加引用计数
void
inc_ref(void *pa){
  int idx = pa2index((uint64)pa);
  if (idx < 0)
    return;

  acquire(&kmem.lock);
  kmem.refcount[idx]++;
  release(&kmem.lock);
}

// 获取引用计数
int
get_ref(void *pa)
{
  int idx = pa2index((uint64)pa);
  if (idx < 0)
    return -1;

  int count;

  acquire(&kmem.lock);
  count=kmem.refcount[idx];
  release(&kmem.lock);
  return count;
}
