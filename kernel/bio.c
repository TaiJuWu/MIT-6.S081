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

#define NUM_BUCKET 13

extern uint ticks;

struct {
  struct spinlock lock;

  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NUM_BUCKET];
  struct spinlock hash_lock[NUM_BUCKET];
} bcache;

uint hash_val(uint key)
{
  return key % NUM_BUCKET;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for(int i = 0; i < NUM_BUCKET; ++i){
    initlock(&bcache.hash_lock[i], "bcache_hash");
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }

  // Create linked list of buffers
  int i = 0;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head[i].next;
    b->prev = &bcache.head[i];
    initsleeplock(&b->lock, "buffer");
    bcache.head[i].next->prev = b;
    bcache.head[i].next = b;
    b->lru_timestamp = ticks;
    i = (i + 1) % NUM_BUCKET;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint hash_idx = hash_val(blockno);
  // Is the block already cached?
  acquire(&bcache.hash_lock[hash_idx]);
  for(b = bcache.head[hash_idx].next; b != &bcache.head[hash_idx]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->lru_timestamp = ticks;
      release(&bcache.hash_lock[hash_idx]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.hash_lock[hash_idx]);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  acquire(&bcache.lock);
  acquire(&bcache.hash_lock[hash_idx]);
  
  // Make share there is no proecess modified bucket when this process
  // give up butcket lock. The circumstance is T1 first enter bget and
  // and check the block is not cached and give up bucket lock. In the
  // same time, T2 enter bget and finish it. The situation cause T1 need
  // to check bucket is already cached again. 
  for(b = bcache.head[hash_idx].next; b != &bcache.head[hash_idx]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      b->lru_timestamp = ticks;
      release(&bcache.hash_lock[hash_idx]);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // If loop run from i=hash_idx, it will cause circular waiting.
  for(int i = 0; i < NUM_BUCKET; ++i){
    if(i == hash_idx) continue;
    acquire(&bcache.hash_lock[i]);
    for(b = bcache.head[i].prev; b != &bcache.head[i]; b = b->prev){
      if(b->refcnt == 0) {
        b->prev->next = b->next;
        b->next->prev = b->prev;
        b->next = bcache.head[hash_idx].next;
        b->prev = &bcache.head[hash_idx];
        bcache.head[hash_idx].next->prev = b;
        bcache.head[hash_idx].next = b;

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        
        release(&bcache.hash_lock[hash_idx]);
        release(&bcache.hash_lock[i]);
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.hash_lock[i]);
  }
  
  panic("bget: no buffers");
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
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint hash_idx = hash_val(b->blockno);
  acquire(&bcache.hash_lock[hash_idx]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[hash_idx].next;
    b->prev = &bcache.head[hash_idx];
    bcache.head[hash_idx].next->prev = b;
    bcache.head[hash_idx].next = b;
  }
  release(&bcache.hash_lock[hash_idx]);
}

void
bpin(struct buf *b) {
  uint hash_idx = hash_val(b->blockno);
  acquire(&bcache.hash_lock[hash_idx]);
  b->refcnt++;
  b->lru_timestamp = ticks;
  release(&bcache.hash_lock[hash_idx]);
}

void
bunpin(struct buf *b) {
  uint hash_idx = hash_val(b->blockno);
  acquire(&bcache.hash_lock[hash_idx]);
  b->refcnt--;
  release(&bcache.hash_lock[hash_idx]);
}


