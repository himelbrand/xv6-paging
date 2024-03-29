#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  pgdir = 0;

 
  
  struct proc *proc = myproc();
  
  // backup and reset proc fields
#ifndef NONE
 int pagesInRAM = proc->pagesInRAM;
  int pagesInSwap = proc->pagesInSwap;
  int totalPageFaults = proc->totalPageFaults;
  int totalPagedOut = proc->totalPagedOut;
  struct freepg freepages[MAX_PSYC_PAGES];
  struct pgdesc swappedpages[MAX_PSYC_PAGES];
  for (i = 0; i < MAX_PSYC_PAGES; i++) {
    freepages[i].va = proc->freepages[i].va;
    proc->freepages[i].va = (char*)0xffffffff;
    freepages[i].next = proc->freepages[i].next;
    proc->freepages[i].next = 0;
    freepages[i].prev = proc->freepages[i].prev;
    proc->freepages[i].prev = 0;
    freepages[i].age = proc->freepages[i].age;
    proc->freepages[i].age = 0;
    swappedpages[i].age = proc->swappedpages[i].age;
    proc->swappedpages[i].age = 0;
    swappedpages[i].va = proc->swappedpages[i].va;
    proc->swappedpages[i].va = (char*)0xffffffff;
  }
  
  proc->pagesInRAM = 0;
  proc->pagesInSwap = 0;
  proc->totalPageFaults = 0;
  proc->totalPagedOut = 0;
  proc->pghead = 0;
  proc->pgtail = 0;
  struct freepg *pghead = proc->pghead;
  struct freepg *pgtail = proc->pgtail;
#endif

   // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) < sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;
  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;
  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(proc->name, last, sizeof(proc->name));

  // Commit to the user image.
  oldpgdir = proc->pgdir;
  proc->pgdir = pgdir;
  proc->sz = sz;
  proc->tf->eip = elf.entry;  // main
  proc->tf->esp = sp;
  // a swap file has been created in fork(), but its content was of the
  // parent process, and is no longer relevant.
  removeSwapFile(proc);
  createSwapFile(proc);
  switchuvm(proc);
  freevm(oldpgdir);
  //cprintf("no. of pages allocated on exec:%d, pid:%d, name:%s\n", proc->pagesInRAM, proc->pid, proc->name);

  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
#ifndef NONE
  proc->pagesInRAM = pagesInRAM;
  proc->pagesInSwap = pagesInSwap;
  proc->totalPageFaults = totalPageFaults;
  proc->totalPagedOut = totalPagedOut;
  proc->pghead = pghead;
  proc->pgtail = pgtail;
  for (i = 0; i < MAX_PSYC_PAGES; i++) {
    proc->freepages[i].va = freepages[i].va;
    proc->freepages[i].next = freepages[i].next;
    proc->freepages[i].prev = freepages[i].prev;
    proc->freepages[i].age = freepages[i].age;
    proc->swappedpages[i].age = swappedpages[i].age;
    proc->swappedpages[i].va = swappedpages[i].va;
  }
#endif
  return -1;
}
