// Stackless coroutines ("protothreads", after Adam Dunkels): a function that can stop part way
// and carry on from the same line on its next call. Locals don't survive a stop, so each
// coroutine keeps its state in a static struct.
//
//   static struct { pt_t pt; int i; } c;
//   PT_THREAD(foo(void)) { PT_BEGIN(&c.pt); ... PT_WAIT_UNTIL(&c.pt, cond); ... PT_END(&c.pt); }
//
// A call returns PT_WAITING while it is part way, PT_DONE when it has finished.
#pragma once

typedef struct { int lc; } pt_t;

#define PT_WAITING 0
#define PT_DONE 1

#define PT_THREAD(sig) int sig
#define PT_INIT(p) ((p)->lc = 0)
#define PT_BEGIN(p) switch ((p)->lc) { case 0:
#define PT_END(p) } (p)->lc = 0; return PT_DONE
// stop here until cond holds (checked again on each call)
#define PT_WAIT_UNTIL(p, cond) do { (p)->lc = __LINE__; case __LINE__: if (!(cond)) return PT_WAITING; } while (0)
#define PT_WAIT_WHILE(p, cond) PT_WAIT_UNTIL(p, !(cond))
// stop until the next call (the next frame)
#define PT_YIELD(p) do { (p)->lc = __LINE__; return PT_WAITING; case __LINE__:; } while (0)
// run a child coroutine to its end (it must be PT_INIT'ed by its own entry, see PT_CALL)
#define PT_CALL(p, call) do { (p)->lc = __LINE__; case __LINE__: if ((call) == PT_WAITING) return PT_WAITING; } while (0)
#define PT_EXIT(p) do { (p)->lc = 0; return PT_DONE; } while (0)
