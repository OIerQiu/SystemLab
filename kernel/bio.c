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

#define NBUCKET 13

extern uint ticks;

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  struct spinlock block[NBUCKET];
  struct buf bucket[NBUCKET]; // 虚拟头节点

  int size;

  // struct spinlock lock;
  // struct buf buf[NBUF];

  // // Linked list of all buffers, through prev/next.
  // // Sorted by how recently the buffer was used.
  // // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  bcache.size = 0;
  initlock(&bcache.lock, "bcache_mainlock");

  for (int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.block[i], "bcache_bucketlock");
  }

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "bcache_sleeplock");
  }

  for (int i = 0; i < NBUCKET; i++) {
    bcache.bucket[i].next = 0;
  }
  // struct buf *b;

  // initlock(&bcache.lock, "bcache");

  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int id = blockno % NBUCKET;
  acquire(&bcache.block[id]);

  for (b = bcache.bucket[id].next; b; b = b->next)
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.block[id]);
      acquiresleep(&b->lock);
      return b;
    }

  acquire(&bcache.lock);

  if (bcache.size < NBUF) {
    struct buf *b = &bcache.buf[bcache.size];
    bcache.size++;
    b->next = bcache.bucket[id].next;
    bcache.bucket[id].next = b;
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    release(&bcache.lock);
    release(&bcache.block[id]);
    acquiresleep(&b->lock);
    return b;
  }

  release(&bcache.lock);

  for (int i = (id + 1) % NBUCKET; i != id; i = (i + 1) % NBUCKET){
    struct buf *cur_buf, *pre_buf, *min_buf = 0, *min_pre_buf = 0;
    uint min_timestamp = -1;

    acquire(&bcache.block[i]);
    pre_buf = &bcache.bucket[i];
    cur_buf = pre_buf->next;

    while (cur_buf) {
      if (cur_buf -> refcnt == 0) {
        if (min_buf == 0) {
          min_buf = cur_buf;
          min_pre_buf = pre_buf;
          min_timestamp = cur_buf->t;
        }
        else if (cur_buf->t < min_timestamp) {
          min_pre_buf = pre_buf;
          min_buf = cur_buf;
          min_timestamp = cur_buf->t;
        }
      }
      pre_buf = cur_buf;
      cur_buf = cur_buf->next;
    }
    
    if (min_buf) {
      min_buf->dev = dev;
      min_buf->blockno = blockno;
      min_buf->valid = 0;
      min_buf->refcnt = 1;
      min_pre_buf->next = min_buf->next;
      min_buf->next = bcache.bucket[id].next;
      bcache.bucket[id].next = min_buf;

      release(&bcache.block[i]);
      release(&bcache.block[id]);
      acquiresleep(&min_buf->lock);
      return min_buf;
    }

    release(&bcache.block[i]);
  }
  
  release(&bcache.block[id]);
  
  panic("bget: no buffers");

  // acquire(&bcache.lock);

  // // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // // Not cached.
  // // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  // panic("bget: no buffers");
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

  int id = b->blockno % NBUCKET;
  acquire(&bcache.block[id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;

    b->t = ticks;
  }
  
  release(&bcache.block[id]);
}

void
bpin(struct buf *b) {
  int id = b->blockno % NBUCKET;
  acquire(&bcache.block[id]);
  b->refcnt++;
  release(&bcache.block[id]);
}

void
bunpin(struct buf *b) {
  int id = b->blockno % NBUCKET;
  acquire(&bcache.block[id]);
  b->refcnt--;
  release(&bcache.block[id]);
}


