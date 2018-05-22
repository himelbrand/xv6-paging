#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#define AGE_INC 0x80000000            // adding 1 to the counter msb
#define SHIFT_COUNTER(x) (x >> 1);    // for shifting the counter

struct
{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int cpuid()
{
  return mycpu() - cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu *
mycpu(void)
{
  int apicid, i;

  if (readeflags() & FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i)
  {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc *
myproc(void)
{
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;
  char *sp;
  int i;
  acquire(&ptable.lock);

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if ((p->kstack = kalloc()) == 0)
  {
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe *)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint *)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context *)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  //init pages data for proc
  for (i = 0; i < MAX_PSYC_PAGES; i++)
  {
    p->freepages[i].va = (char *)0xffffffff;
    p->freepages[i].next = 0;
    p->freepages[i].prev = 0;
    p->freepages[i].age = 0;
    p->swappedpages[i].age = 0;
    p->swappedpages[i].swaploc = 0;
    p->swappedpages[i].va = (char *)0xffffffff;
  }
  p->pagesinmem = 0;
  p->pagesinswapfile = 0;
  p->totalPageFaultCount = 0;
  p->totalPagedOutCount = 0;
  p->pghead = 0;
  p->pgtail = 0;
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if ((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0; // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if (n > 0)
  {
    if ((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  else if (n < 0)
  {
    if ((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int fork(void)
{
  int i, pid, j;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy process state from proc.
      //  cprintf("curproc->sz = %d\n",curproc->sz);

  if ((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0)
  {
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->pagesinmem = curproc->pagesinmem;
  np->pagesinswapfile = curproc->pagesinswapfile;
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for (i = 0; i < NOFILE; i++)
    if (curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  //paging stuff
  createSwapFile(np); //create swap file
  char buf[PGSIZE / 2] = "";
  int offset = 0;
  int nread = 0;
  // read the parent's swap file in chunks of size PGDIR/2, otherwise for some
  // reason, you get "panic acquire" if buf is ~4000 bytes
  // copying swapfile data from parent
  if (curproc->pid > 2 || (strcmp(curproc->name, "init") != 0 && strcmp(curproc->name, "sh") != 0))
  {
    while ((nread = readFromSwapFile(curproc, buf, offset, PGSIZE / 2)) != 0)
    {
      if (writeToSwapFile(np, buf, offset, nread) == -1)
        panic("fork: error while writing the parent's swap file to the child");
      offset += nread;
    }
  }
  //copy arrays of pages data from parent
  for (i = 0; i < MAX_PSYC_PAGES; i++)
  {
    np->freepages[i].va = curproc->freepages[i].va;
    np->freepages[i].age = curproc->freepages[i].age;
    np->swappedpages[i].age = curproc->swappedpages[i].age;
    np->swappedpages[i].va = curproc->swappedpages[i].va;
   // cprintf("swapped i=%d , va=%x\n",i,(uint)np->swappedpages[i].va);
    //cprintf("free i=%d , va=%x\n",i,(uint)np->freepages[i].va);
   // cprintf("swaploc :%d\n",curproc->swappedpages[i].swaploc);

    np->swappedpages[i].swaploc = curproc->swappedpages[i].swaploc;
  }

  //relink linked list of free pages in child
  for (i = 0; i < MAX_PSYC_PAGES; i++)
    for (j = 0; j < MAX_PSYC_PAGES; ++j)
    {
      if (np->freepages[j].va == curproc->freepages[i].next->va)
        np->freepages[i].next = &np->freepages[j];
      if (np->freepages[j].va == curproc->freepages[i].prev->va)
        np->freepages[i].prev = &np->freepages[j];
    }
#if defined(SCFIFO) || defined(AQ)
  for (i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (curproc->pghead->va == np->freepages[i].va)
    {
      //TODO delete       cprintf("\nfork: head copied!\n\n");
      np->pghead = &np->freepages[i];
    }
    if (curproc->pgtail->va == np->freepages[i].va)
    {
      np->pgtail = &np->freepages[i];
      //cprintf("\nfork: head copied!\n\n");
    }
  }
#endif

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (curproc->ofile[fd])
    {
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }
  if (removeSwapFile(curproc) != 0)
    panic("exit: error deleting swap file");

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == curproc)
    {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->parent != curproc)
        continue;
      havekids = 1;
      if (p->state == ZOMBIE)
      {
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || curproc->killed)
    {
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock); //DOC: wait-sleep
  }
}
void aqUpdate(void){
  //cprintf("?\n");
  struct freepg *mover,*prev,*temp,*oldpgtail;
  struct proc *proc = myproc();
  mover=proc->pghead;
  oldpgtail = proc->pgtail; // to avoid infinite loop if everyone was accessed
 // cprintf("proc->pghead: %s\n",proc->name);
  while(oldpgtail != mover){
     // cprintf("?\n");

    if(mover && checkAccBit(mover->va,1) && mover != proc->pghead){
      temp = mover->prev;
      prev = temp->prev;
      prev->next=mover;
      temp->next = mover->next;
       mover->next->prev = temp;
      temp->prev = mover;
      mover->next = temp;
      mover->prev = prev;
      
      if(temp == proc->pghead){
        proc->pghead = mover;
      }
      mover = temp->next;
    }else{
       mover = mover->next;
    }
  }
 // if(strcmp(proc->name,"initcode") != 0)
 // cprintf("proc->pghead:done %s\n",proc->name);
}
void updateFPages(void){
  #ifdef AQ
    aqUpdate();
  #else
  #ifdef LAPA
  #else
  #ifdef NFUA
  #endif
  #endif
  #endif
}
//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;)
  {
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if (p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);

      #if defined(AQ) || defined(LAPA) || defined(NFUA)
        updateFPages();
      #endif
      switchkvm();
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&ptable.lock))
    panic("sched ptable.lock");
  if (mycpu()->ncli != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (readeflags() & FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  acquire(&ptable.lock); //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first)
  {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (p == 0)
    panic("sleep");

  if (lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if (lk != &ptable.lock)
  {                        //DOC: sleeplock0
    acquire(&ptable.lock); //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if (lk != &ptable.lock)
  { //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->killed = 1;
      // Wake process from sleep if necessary.
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [EMBRYO] "embryo",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %d %d %d %d %s", p->pid, state, p->pagesinmem, p->pagesinswapfile, p->totalPageFaultCount, p->totalPagedOutCount, p->name);
    if (p->state == SLEEPING)
    {
      getcallerpcs((uint *)p->context->ebp + 2, pc);
      for (i = 0; i < 10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    // cprintf("[0]=%x , [1]=%x [2]=%x what??\n",p->freepages[0].va,p->freepages[1].va,p->freepages[2].va);
    cprintf("\n");
  }
}

// Purpose: Iterate over all the procceses pages
// and update the age counter
void updateNFU(void){
  struct proc *p;
  int i;
  pte_t *pte, *pde, *pgtab;

  acquire(&ptable.lock);
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if((p->state == SLEEPING || p->state == RUNNABLE || p->state == RUNNING) && (p->pid > 2)){
      for (i = 0; i < MAX_PSYC_PAGES; i++){
        
        if (p->freepages[i].va == (char*)0xffffffff)
          continue;
        // adding 1 to the counters
        ++p->freepages[i].age;
        ++p->swappedpages[i].age;
        
        pde = &p->pgdir[PDX(p->freepages[i].va)];

        // checking if the fist page table is present
        if(*pde & PTE_P){
          pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
          pte = &pgtab[PTX(p->freepages[i].va)];
        }

        else 
          pte = 0;

        if(pte){
          // checking if the current page was access than add 1 to counter
          if( *pte & PTE_A){
            p->freepages[i].age = 0;
          }
        }
      }
    }
  }
  release(&ptable.lock);
}

