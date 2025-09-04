#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader {
  int n;
  int block[LOGSIZE];
};

struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct log blog;

static void recover_from_log(void);
static void commit();

void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&blog.lock, "log");
  blog.start = sb->logstart;
  blog.size = sb->nlog;
  blog.dev = dev;
  recover_from_log();
}

// Copy committed blocks from log to their home location
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < blog.lh.n; tail++) {
    struct buf *lbuf = bread(blog.dev, blog.start+tail+1); // read log block
    struct buf *dbuf = bread(blog.dev, blog.lh.block[tail]); // read dst
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
    bwrite(dbuf);  // write dst to disk
    if(recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(blog.dev, blog.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  blog.lh.n = lh->n;
  for (i = 0; i < blog.lh.n; i++) {
    blog.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
static void
write_head(void)
{
  struct buf *buf = bread(blog.dev, blog.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = blog.lh.n;
  for (i = 0; i < blog.lh.n; i++) {
    hb->block[i] = blog.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // if committed, copy from log to disk
  blog.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
void
begin_op(void)
{
  acquire(&blog.lock);
  while(1){
    if(blog.committing){
      sleep(&blog, &blog.lock);
    } else if(blog.lh.n + (blog.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // this op might exhaust log space; wait for commit.
      sleep(&blog, &blog.lock);
    } else {
      blog.outstanding += 1;
      release(&blog.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&blog.lock);
  blog.outstanding -= 1;
  if(blog.committing)
    panic("blog.committing");
  if(blog.outstanding == 0){
    do_commit = 1;
    blog.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing blog.outstanding has decreased
    // the amount of reserved space.
    wakeup(&blog);
  }
  release(&blog.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&blog.lock);
    blog.committing = 0;
    wakeup(&blog);
    release(&blog.lock);
  }
}

// Copy modified blocks from cache to log.
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < blog.lh.n; tail++) {
    struct buf *to = bread(blog.dev, blog.start+tail+1); // log block
    struct buf *from = bread(blog.dev, blog.lh.block[tail]); // cache block
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (blog.lh.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(0); // Now install writes to home locations
    blog.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b) // 将 b 加入 blog.lh.block 数组，交由 log 系统管理
{
  int i;

  acquire(&blog.lock);
  if (blog.lh.n >= LOGSIZE || blog.lh.n >= blog.size - 1)
    panic("too big a transaction");
  if (blog.outstanding < 1)
    panic("log_write outside of trans");

  for (i = 0; i < blog.lh.n; i++) {
    if (blog.lh.block[i] == b->blockno)   // log absorption
      break;
  }
  blog.lh.block[i] = b->blockno;
  if (i == blog.lh.n) {  // Add new block to log?
    bpin(b);
    blog.lh.n++;
  }
  release(&blog.lock);
}

