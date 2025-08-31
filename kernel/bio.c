// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct bucket {
  struct spinlock lock;
  struct buf head;
};
#define NBUCKET 13
struct bcache {
  struct spinlock lock;       // 分配锁
  struct buf buf[NBUF];       // 所有缓冲区
  struct bucket bucket[NBUCKET];
} bcache;

void binit(void) {
  initlock(&bcache.lock, "bcache");

  // 初始化每个桶
  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.bucket[i].lock, "bcache.bucket");
    bcache.bucket[i].head.next = &bcache.bucket[i].head;
  }

  // 所有缓冲区初始分配到桶0
  for (int i = 0; i < NBUF; i++) {
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->next = bcache.bucket[0].head.next;
    bcache.bucket[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno) {
  struct buf *b;
  uint hash = blockno % NBUCKET;
  struct bucket *buk = &bcache.bucket[hash];
  
  // 阶段1：快速检查
  acquire(&buk->lock);
  for (b = buk->head.next; b != &buk->head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&buk->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&buk->lock);

  // 阶段2：全局分配
  acquire(&bcache.lock);
  acquire(&buk->lock);  // 重新获取目标桶锁
  
  // 重检查目标桶
  for (b = buk->head.next; b != &buk->head; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&buk->lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  // 阶段3：寻找空闲缓冲区
  struct buf *victim = 0;
  int victim_bucket = -1;
  
  // 先检查目标桶
  for (b = buk->head.next; b != &buk->head; b = b->next) {
    if (b->refcnt == 0) {
      victim = b;
      victim_bucket = hash;
      break;
    }
  }
  
  // 扫描其他桶
  struct bucket *src = 0;
  if (!victim) {
    for (int i = 0; i < NBUCKET; i++) {
      if (i == hash) continue;
      
      src = &bcache.bucket[i];
      if (!holding(&src->lock)) {
        acquire(&src->lock);
      }
      
      for (b = src->head.next; b != &src->head; b = b->next) {
        if (b->refcnt == 0) {
          victim = b;
          victim_bucket = i;
          goto found;
        }
      }
      release(&src->lock);  // 未找到，释放当前桶锁
    }
  }
  
found:
  if (!victim) {
    release(&buk->lock);
    release(&bcache.lock);
    panic("bget: no buffers");
  }
  
  // 阶段4：缓冲区迁移
  if (victim_bucket != hash) {
    // src 已在扫描时获取
    // 从源桶移除
    struct buf *prev = &src->head;
    while (prev->next != victim) prev = prev->next;
    prev->next = victim->next;
    
    // 插入目标桶
    victim->next = buk->head.next;
    buk->head.next = victim;
  }
  
  // 初始化缓冲区
  victim->dev = dev;
  victim->blockno = blockno;
  victim->valid = 0;
  victim->refcnt = 1;
  
  // 精确释放锁
  if (victim_bucket != hash) {
    release(&src->lock);  // 释放源桶锁
  }
  release(&buk->lock);    // 释放目标桶锁
  release(&bcache.lock);  // 释放全局锁
  
  acquiresleep(&victim->lock);
  return victim;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
  if (!holdingsleep(&b->lock)) panic("brelse");

  releasesleep(&b->lock);

  uint hash = b->blockno % NBUCKET;
  struct bucket *buk = &bcache.bucket[hash];

  acquire(&buk->lock);
  b->refcnt--;
  release(&buk->lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.bucket[b->blockno%NBUCKET].lock);
  b->refcnt++;
  release(&bcache.bucket[b->blockno%NBUCKET].lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.bucket[b->blockno%NBUCKET].lock);
  b->refcnt--;
  release(&bcache.bucket[b->blockno%NBUCKET].lock);
}


