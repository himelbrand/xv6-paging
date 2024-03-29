#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

#define BUF_SIZE PGSIZE / 4

extern char data[]; // defined by kernel.ld
pde_t *kpgdir;      // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X | STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X | STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if (*pde & PTE_P)
  { //if present
    pgtab = (pte_t *)P2V(PTE_ADDR(*pde));
  }
  else
  {
    if (!alloc || (pgtab = (pte_t *)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}


int checkAndClearFlag(char *va,int clear,int flag)
{ //checks if page at va has Access bit on and clears the bit
  uint accessed;
  struct proc *proc = myproc();
  pte_t *pte = walkpgdir(proc->pgdir, (void *)va, 0);
  if (!*pte)
    panic("checkAndClearFlag: pte1 is empty");
  accessed = (*pte) & flag;
  if(clear) (*pte) &= ~flag;
  #ifdef SCFIFO
    if((uint)va <= 0x2000 && flag == PTE_A)
    (*pte) |= PTE_A;
  #endif
  return accessed;
}
// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char *)PGROUNDDOWN((uint)va);
  last = (char *)PGROUNDDOWN(((uint)va) + size - 1);
  for (;;)
  {
    if ((pte = walkpgdir(pgdir, a, perm)) == 0)
      return -1;
    if (*pte & PTE_P)
      panic("remap");    
    if (perm & PTE_PG)
      *pte = pa | perm | PTE_PG;
    else
      *pte = pa | perm | PTE_P;


    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
 

  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap
{
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
    {(void *)KERNBASE, 0, EXTMEM, PTE_W},            // I/O space
    {(void *)KERNLINK, V2P(KERNLINK), V2P(data), 0}, // kern text+rodata
    {(void *)data, V2P(data), PHYSTOP, PTE_W},       // kern data+memory
    {(void *)DEVSPACE, DEVSPACE, 0, PTE_W},          // more devices
};

// Added: helper function to get the number of '1' bits
//        in a given unsigned number
int
getOneBits(uint num){
  int ans = 0;
  while (num != 0){
    if(num % 2 != 0)
      ans++;
    num = num / 2;
  }
  return ans;
}

// Set up kernel part of a page table.
pde_t *
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if ((pgdir = (pde_t *)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void *)DEVSPACE)
    panic("PHYSTOP too high");
  for (k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if (mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                 (uint)k->phys_start, k->perm) < 0)
    {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void switchkvm(void)
{
  lcr3(V2P(kpgdir)); // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void switchuvm(struct proc *p)
{
  if (p == 0)
    panic("switchuvm: no process");
  if (p->kstack == 0)
    panic("switchuvm: no kstack");
  if (p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts) - 1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort)0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir)); // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W | PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if ((uint)addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walkpgdir(pgdir, addr + i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);

    if (sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if (readi(ip, P2V(pa), offset + i, n) != n)
      return -1;
  }
  return 0;
}






// NFU page swaping policy
// TODO, split cases to NFUA and LAPA
void nfuaSwap(uint addr) {
  int i, j;
  struct proc  *proc =myproc();
  uint maxIndex = 0xffffffff, maxAge = 0;// MAX_POSSIBLE;
  char buf[BUF_SIZE];
  pte_t *pte1, *pte2;
  struct freepg *chosen;


  for (j = 3; j < MAX_PSYC_PAGES; j++)
    if (proc->freepages[j].va != (char*)0xffffffff){
      if (proc->freepages[j].age > maxAge){
        maxAge = proc->freepages[j].age;
        maxIndex = j;
      }
    }
  maxIndex = maxIndex == -1 && proc->freepages[3].age == maxAge ? 3 : maxIndex;

  if(maxIndex == -1)
    panic("nfuSwap: no free page to swap???");
  chosen = &proc->freepages[maxIndex];

  //find the address of the page table entry to copy into the swap file
  pte1 = walkpgdir(proc->pgdir, (void*)chosen->va, 0);
  if (!*pte1)
    panic("nfuSwap: pte1 is empty");

//  TODO verify: b4 accessing by writing to file,
//  update accessed bit and age in case it misses a clock tick?
//  be extra careful not to double add by locking
  acquire(&tickslock);
  //TODO delete cprintf("acquire(&tickslock)\n");
  if((*pte1) & PTE_A){
    ++chosen->age;
    *pte1 &= ~PTE_A;
    //TODO delete cprintf("========\n\nWOW! Matan was right!\n(never saw this actually printed)=======\n\n");
  }
  release(&tickslock);

  //find a swap file page descriptor slot
  for (i = 0; i < MAX_PSYC_PAGES; i++){
    if (proc->swappedpages[i].va == (char*)PTE_ADDR(addr))
      goto foundswappedpageslot;
  }
  panic("nfuSwap: no slot for swapped page");

foundswappedpageslot:

  proc->swappedpages[i].va = chosen->va;
  //assign the physical page to addr in the relevant page table
  pte2 = walkpgdir(proc->pgdir, (void*)addr, 0);
  if (!*pte2)
    panic("nfuSwap: pte2 is empty");
  //set page table entry
  //TODO verify we're not setting PTE_U where we shouldn't be...
  *pte2 = PTE_ADDR(*pte1) | PTE_U | PTE_W | PTE_P;// access bit is zeroed...

  for (j = 0; j < 4; j++) {
    int loc = (i * PGSIZE) + ((PGSIZE / 4) * j);
    // cprintf("i:%d j:%d loc:0x%x\n", i,j,loc);//TODO delete
    int addroffset = ((PGSIZE / 4) * j);
    // int read, written;
    memset(buf, 0, BUF_SIZE);
    //copy the new page from the swap file to buf
    // read =
    readFromSwapFile(proc, buf, loc, BUF_SIZE);
    // cprintf("read:%d\n", read);//TODO delete
    //copy the old page from the memory to the swap file
    //written =
    writeToSwapFile(proc, (char*)(P2V_WO(PTE_ADDR(*pte1)) + addroffset), loc, BUF_SIZE);
    // cprintf("written:%d\n", written);//TODO delete
    //copy the new page from buf to the memory
    memmove((void*)(PTE_ADDR(addr) + addroffset), (void*)buf, BUF_SIZE);
  }
  //update the page table entry flags, reset the physical page address
  *pte1 = PTE_U | PTE_W | PTE_PG;
  //update l to hold the new va
  //l->next = proc->head;
  //proc->head = l;
  chosen->va = (char*)PTE_ADDR(addr);
  // was this missed some how???
  chosen->age = 0;
}

// LAPA page swaping policy
// TODO, split cases to NFUA and LAPA
void lapaSwap(uint addr) {
  int i, j;
  struct proc  *proc =myproc();
  uint maxIndex = 0xffffffff, numOfOnes = 32;// MAX_POSSIBLE;
  char buf[BUF_SIZE];
  pte_t *pte1, *pte2;
  struct freepg *chosen;


  for (j = 3; j < MAX_PSYC_PAGES; j++)
    if (proc->freepages[j].va != (char*)0xffffffff){
      if (getOneBits(proc->freepages[j].age) < numOfOnes){
        numOfOnes = getOneBits(proc->freepages[j].age);
        maxIndex = j;
      }
    }
  maxIndex = maxIndex == -1 && getOneBits(proc->freepages[3].age) == numOfOnes ? 3 : maxIndex;

  if(maxIndex == -1)
    panic("nfuSwap: no free page to swap???");
  chosen = &proc->freepages[maxIndex];

  //find the address of the page table entry to copy into the swap file
  pte1 = walkpgdir(proc->pgdir, (void*)chosen->va, 0);
  if (!*pte1)
    panic("nfuSwap: pte1 is empty");

//  TODO verify: b4 accessing by writing to file,
//  update accessed bit and age in case it misses a clock tick?
//  be extra careful not to double add by locking
  acquire(&tickslock);
  //TODO delete cprintf("acquire(&tickslock)\n");
  if((*pte1) & PTE_A){
    ++chosen->age;
    *pte1 &= ~PTE_A;
    //TODO delete cprintf("========\n\nWOW! Matan was right!\n(never saw this actually printed)=======\n\n");
  }
  release(&tickslock);

  //find a swap file page descriptor slot
  for (i = 0; i < MAX_PSYC_PAGES; i++){
    if (proc->swappedpages[i].va == (char*)PTE_ADDR(addr))
      goto foundswappedpageslot;
  }
  panic("nfuSwap: no slot for swapped page");

foundswappedpageslot:

  proc->swappedpages[i].va = chosen->va;
  //assign the physical page to addr in the relevant page table
  pte2 = walkpgdir(proc->pgdir, (void*)addr, 0);
  if (!*pte2)
    panic("nfuSwap: pte2 is empty");
  //set page table entry
  //TODO verify we're not setting PTE_U where we shouldn't be...
  *pte2 = PTE_ADDR(*pte1) | PTE_U | PTE_W | PTE_P;// access bit is zeroed...

  for (j = 0; j < 4; j++) {
    int loc = (i * PGSIZE) + ((PGSIZE / 4) * j);
    // cprintf("i:%d j:%d loc:0x%x\n", i,j,loc);//TODO delete
    int addroffset = ((PGSIZE / 4) * j);
    // int read, written;
    memset(buf, 0, BUF_SIZE);
    //copy the new page from the swap file to buf
    // read =
    readFromSwapFile(proc, buf, loc, BUF_SIZE);
    // cprintf("read:%d\n", read);//TODO delete
    //copy the old page from the memory to the swap file
    //written =
    writeToSwapFile(proc, (char*)(P2V_WO(PTE_ADDR(*pte1)) + addroffset), loc, BUF_SIZE);
    // cprintf("written:%d\n", written);//TODO delete
    //copy the new page from buf to the memory
    memmove((void*)(PTE_ADDR(addr) + addroffset), (void*)buf, BUF_SIZE);
  }
  //update the page table entry flags, reset the physical page address
  *pte1 = PTE_U | PTE_W | PTE_PG;
  //update l to hold the new va
  //l->next = proc->head;
  //proc->head = l;
  chosen->va = (char*)PTE_ADDR(addr);
  // was this missed some how???
  chosen->age = 0;
}


void initFreePage(char *va)
{
#if defined(SCFIFO) || defined(AQ)
  int i;
  struct proc *proc = myproc();
  for (i = 0; i < MAX_PSYC_PAGES; i++)
    if (proc->freepages[i].va == (char *)0xffffffff)
      goto foundlinked;
  cprintf("panic follows, pid:%d, name:%s\n", proc->pid, proc->name);
  panic("initFreePage: no free pages");
foundlinked:
  proc->freepages[i].va = va;
  proc->freepages[i].next = proc->pghead;
  proc->freepages[i].prev = 0;
  if (proc->pghead != 0) // old head points back to new head
    proc->pghead->prev = &proc->freepages[i];
  else //head == 0 so first link inserted is also the tail
    proc->pgtail = &proc->freepages[i];
  proc->pghead = &proc->freepages[i];

#elif defined(NFUA) || defined(LAPA)
 struct proc * proc =myproc();
  int i;
  for (i = 0; i < MAX_PSYC_PAGES; i++)
    if (proc->freepages[i].va == (char*)0xffffffff)
      goto found;
  cprintf("panic follows, pid:%d, name:%s\n", proc->pid, proc->name);
  panic("recordNewPage: no free pages");
  found:
    proc->freepages[i].va = va;
#endif

  myproc()->pagesInRAM++;
 
}
struct freepg *scWrite(char *va)
{
  int i;
  struct freepg *curr, *oldpgtail;
  struct proc *proc = myproc();
  for (i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (proc->swappedpages[i].va == (char *)0xffffffff)
      goto foundswappedpageslot;
  }
  panic("writePageToSwapFile: FIFO no slot for swapped page");

foundswappedpageslot:
  if (proc->pghead == 0)
    panic("scWrite: proc->pghead is NULL");
  if (proc->pghead->next == 0)
    panic("scWrite: single page in phys mem");

  curr = proc->pgtail;
  oldpgtail = proc->pgtail; // to avoid infinite loop 
  do
  {
    //move curr from pgtail to pghead
    proc->pgtail = proc->pgtail->prev;
    proc->pgtail->next = 0;
    curr->prev = 0;
    curr->next = proc->pghead;
    proc->pghead->prev = curr;
    proc->pghead = curr;
    curr = proc->pgtail;
  } while (checkAndClearFlag(proc->pghead->va,1,PTE_A) && curr != oldpgtail);

if((uint)proc->pghead->va <= 0x2000){//not to swap user data pages
    curr = proc->pgtail;
  oldpgtail = proc->pgtail;
    do
  {
    //move curr from pgtail to pghead
    proc->pgtail = proc->pgtail->prev;
    proc->pgtail->next = 0;
    curr->prev = 0;
    curr->next = proc->pghead;
    proc->pghead->prev = curr;
    proc->pghead = curr;
    curr = proc->pgtail;
  } while (checkAndClearFlag(proc->pghead->va,1,PTE_A) && curr != oldpgtail);
  }
  //make the swap
  proc->swappedpages[i].va = proc->pghead->va;
  int num = 0;
  //write head of physical pages to swapfile
  if ((num = writeToSwapFile(proc, (char *)PTE_ADDR(proc->pghead->va), i * PGSIZE, PGSIZE)) == 0)
    return 0;

  pte_t *pte1 = walkpgdir(proc->pgdir, (void *)proc->pghead->va, 0);
  if (!*pte1)
    panic("writePageToSwapFile: pte1 is empty");

  kfree((char *)PTE_ADDR(P2V_WO(*walkpgdir(proc->pgdir, proc->pghead->va, 0))));
  *pte1 = PTE_W | PTE_U | PTE_PG;
  ++proc->totalPagedOut;
  ++proc->pagesInSwap;

  //refresh TLB
  lcr3(V2P(proc->pgdir));
  proc->pghead->va = va;

  return proc->pghead;
}


struct freepg *lapaWrite(char *va) {
    cprintf("lapa swap addr:%x\n",va);

  int i, j;
  uint ind = -1, maxOnes = 32; //MAX_POSSIBLE;
  struct freepg *chosen;
  struct proc * proc =myproc();

  for (i = 0; i < MAX_PSYC_PAGES; i++){
    // checking for available slot 
    if (proc->swappedpages[i].va == (char*)0xffffffff)
      goto foundswappedpageslot;
  }
  panic("writePageToSwapFile: LAPA no slot for swapped page");

foundswappedpageslot:
  for (j = 3; j < MAX_PSYC_PAGES; j++){
  //  cprintf("one bits in page j=%d age is: %d \nand freepages va is: ", j, getOneBits(proc->freepages[j].age), proc->freepages[j].va);
    if (proc->freepages[j].va != (char*)0xffffffff){
      if (getOneBits(proc->freepages[j].age) < maxOnes){
        maxOnes = getOneBits(proc->freepages[j].age);
        ind = j;
      }
    }
  }
  ind = ind == -1 && getOneBits(proc->freepages[3].age) == maxOnes ? 3 : ind;
  if(ind == -1)
    panic("lapaWrite: no free page to swap");
  chosen = &proc->freepages[ind];

  pte_t *pte1 = walkpgdir(proc->pgdir, (void*)chosen->va, 0);
  if (!*pte1)
    panic("lapaWrite: pte1 is empty");

//  b4 accessing by writing to file,
//  update accessed bit and age in case it misses a clock tick?
//  be extra careful not to double add by locking
  acquire(&tickslock);
  if((*pte1) & PTE_A){
    ++chosen->age;
    *pte1 &= ~PTE_A;    
  }
  release(&tickslock);

  //make swap
  proc->swappedpages[i].va = chosen->va;
  int num = 0;
  if ((num = writeToSwapFile(proc, (char*)PTE_ADDR(chosen->va), i * PGSIZE, PGSIZE)) == 0)
    return 0;

  kfree((char*)PTE_ADDR(P2V_WO(*walkpgdir(proc->pgdir, chosen->va, 0))));
  *pte1 = PTE_W | PTE_U | PTE_PG;
  ++proc->totalPagedOut;
  ++proc->pagesInSwap;

  lcr3(V2P(proc->pgdir));
  chosen->va = va;

  return chosen;
}




struct freepg *nfuaWrite(char *va) {
  int i, j;
  uint maxIndex = -1, maxAge = 0; //MAX_POSSIBLE;
  struct freepg *chosen;
  struct proc * proc =myproc();

  for (i = 0; i < MAX_PSYC_PAGES; i++){
    // checking for available slot 
    if (proc->swappedpages[i].va == (char*)0xffffffff)
      goto foundswappedpageslot;
  }
  panic("writePageToSwapFile: NFUA no slot for swapped page");

foundswappedpageslot:
  for (j = 3; j < MAX_PSYC_PAGES; j++){
    if (proc->freepages[j].va != (char*)0xffffffff){
      // cprintf("proc->freepages[j].va = %x ,age=%x , j=%d , maxAge=%x\n",proc->freepages[j].va,proc->freepages[j].age,j,maxAge);
      if (proc->freepages[j].age > maxAge){
        maxAge = proc->freepages[j].age;
        maxIndex = j;
      }
    }
  }
  maxIndex = maxIndex == -1 && proc->freepages[3].age == maxAge ? 3 : maxIndex;
  if(maxIndex == -1)
    panic("nfuaWrite: no free page to swap");
  chosen = &proc->freepages[maxIndex];

  pte_t *pte1 = walkpgdir(proc->pgdir, (void*)chosen->va, 0);
  if (!*pte1)
    panic("writePageToSwapFile: pte1 is empty");

//  b4 accessing by writing to file,
//  update accessed bit and age in case it misses a clock tick?
//  be extra careful not to double add by locking
  acquire(&tickslock);
  if((*pte1) & PTE_A){
    ++chosen->age;
    *pte1 &= ~PTE_A;    
  }
  release(&tickslock);

  //make swap
  proc->swappedpages[i].va = chosen->va;
  int num = 0;
  if ((num = writeToSwapFile(proc, (char*)PTE_ADDR(chosen->va), i * PGSIZE, PGSIZE)) == 0)
    return 0;

  kfree((char*)PTE_ADDR(P2V_WO(*walkpgdir(proc->pgdir, chosen->va, 0))));
  *pte1 = PTE_W | PTE_U | PTE_PG;
  ++proc->totalPagedOut;
  ++proc->pagesInSwap;

  lcr3(V2P(proc->pgdir));
  chosen->va = va;

  return chosen;
}


struct freepg *aqWrite(char *va)
{
  int i;
  struct freepg *curr;
  struct proc *proc = myproc();
  for (i = 3; i < MAX_PSYC_PAGES; i++)
  {
    if (proc->swappedpages[i].va == (char *)0xffffffff)
      goto foundswappedpageslot;
  }
  panic("writePageToSwapFile: AQ no slot for swapped page");

foundswappedpageslot:
  if (proc->pghead == 0)
    panic("aqWrite: proc->pghead is NULL");
  if (proc->pghead->next == 0)
    panic("aqWrite: single page in phys mem");
do{
  curr = proc->pgtail;
  //move curr from pgtail to pghead
  proc->pgtail = proc->pgtail->prev;
  proc->pgtail->next = 0;
  curr->prev = 0;
  curr->next = proc->pghead;
  proc->pghead->prev = curr;
  proc->pghead = curr;
}while((uint)proc->pghead->va <= 0x2000);

  //make the swap
  proc->swappedpages[i].va = proc->pghead->va;
  int num = 0;
  //write head of physical pages to swapfile
  if ((num = writeToSwapFile(proc, (char *)PTE_ADDR(proc->pghead->va), i * PGSIZE, PGSIZE)) == 0)
    return 0;

  pte_t *pte1 = walkpgdir(proc->pgdir, (void *)proc->pghead->va, 0);
  if (!*pte1)
    panic("writePageToSwapFile: pte1 is empty");

  kfree((char *)PTE_ADDR(P2V_WO(*walkpgdir(proc->pgdir, proc->pghead->va, 0))));
  *pte1 = PTE_W | PTE_U | PTE_PG;
  ++proc->totalPagedOut;
  ++proc->pagesInSwap;

  //refresh TLB
  lcr3(V2P(proc->pgdir));
  proc->pghead->va = va;

  return proc->pghead;
}

struct freepg *writePageToSwapFile(char *va)
{
#ifdef SCFIFO
  return scWrite(va);
#else
#ifdef AQ
  return aqWrite(va);
#else
#ifdef NFUA
    return nfuaWrite(va);
#else
#ifdef LAPA
    return lapaWrite(va);
#endif
#endif
#endif
#endif
  return 0;
}
// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;
#ifndef NONE
  uint newpage = 1;
  struct freepg *pg;
#endif

  if (newsz >= KERNBASE)
    return 0;
  if (newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for (; a < newsz; a += PGSIZE)
  {
#ifndef NONE
    if (myproc()->pagesInRAM >= MAX_PSYC_PAGES)
    {
      if ((pg = writePageToSwapFile((char *)a)) == 0)
        panic("allocuvm: error writing page to swap file");
#ifdef CSFIFO
      pg->va = (char *)a;
      pg->next = myproc()->pghead;
      myproc()->pghead = pg;
#endif
      newpage = 0;
    }
#endif
    mem = kalloc();
    if (mem == 0)
    {
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
#ifndef NONE
    if (newpage)
    {
      initFreePage((char *)a);
    }
#endif
    memset(mem, 0, PGSIZE);
    if (mappages(pgdir, (char *)a, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0)
    {
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;
  int i;
  struct proc *proc = myproc();
  if (newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for (; a < oldsz; a += PGSIZE)
  {
    pte = walkpgdir(pgdir, (char *)a, 0);
    if (!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if ((*pte & PTE_P) != 0 && (*pte & PTE_PG) == 0 )
    {
      pa = PTE_ADDR(*pte);
      if (pa == 0){
        panic("kfree");

      }
      if (proc->pgdir == pgdir)
      {
        /*
        The process itself is deallocating pages via sbrk() with a negative
        argument. Update proc's data structure accordingly.
        */
#ifndef NONE
        for (i = 0; i < MAX_PSYC_PAGES; i++)
        {
          if (proc->freepages[i].va == (char *)a)
            goto founddeallocuvmPTEP;
        }

        panic("deallocuvm: entry not found in proc->freepages");
      founddeallocuvmPTEP:
        myproc()->freepages[i].va = (char *)0xffffffff;
#if defined(SCFIFO) || defined(AQ)
        if (proc->pghead == &proc->freepages[i])
        {
          proc->pghead = proc->freepages[i].next;
          if (proc->pghead != 0)
            proc->pghead->prev = 0;
          goto doneLooking;
        }
        if (proc->pgtail == &proc->freepages[i])
        {
          proc->pgtail = proc->freepages[i].prev;
          if (proc->pgtail != 0) 
            proc->pgtail->next = 0;
          goto doneLooking;
        }
        struct freepg *l = proc->pghead;
        while (l->next != 0 && l->next != &proc->freepages[i])
        {
          l = l->next;
        }
        l->next = proc->freepages[i].next;
        if (proc->freepages[i].next != 0)
        {
          proc->freepages[i].next->prev = l;
        }

      doneLooking:
        proc->freepages[i].next = 0;
        proc->freepages[i].prev = 0;
#endif
#endif
        proc->pagesInRAM--;
      }
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
    else if (*pte & PTE_PG && proc->pgdir == pgdir)
    {
      /*
      The process itself is deallocating pages via sbrk() with a negative
      argument. Update proc's data structure accordingly.
      */
      for (i = 0; i < MAX_PSYC_PAGES; i++)
      {
        if (proc->swappedpages[i].va == (char *)a)
          goto founddeallocuvmPTEPG;
      }
      panic("deallocuvm: entry not found in proc->swappedpages");
    founddeallocuvmPTEPG:
      proc->swappedpages[i].va = (char *)0xffffffff;
      proc->swappedpages[i].age = 0;
      proc->pagesInSwap--;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void freevm(pde_t *pgdir)
{
  uint i;

  if (pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for (i = 0; i < NPDENTRIES; i++)
  {
    if (pgdir[i] & PTE_P)
    {
      char *v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char *)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if (pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t *
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if ((d = setupkvm()) == 0)
    return 0;
  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walkpgdir(pgdir, (void *)i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if (!(*pte & PTE_P) && !(*pte & PTE_PG))//changed condition to include PG
    {
      panic("copyuvm: page not present");
    }
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
   
    if ((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char *)P2V(pa), PGSIZE);
    if (mappages(d, (void *)i, PGSIZE, V2P(mem), flags) < 0)
      goto bad;
    if(!(flags & PTE_P))//maybe not needed
      (*pte) ^= PTE_P;
    
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char *
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if ((*pte & PTE_P) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  return (char *)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char *)p;
  while (len > 0)
  {
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char *)va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if (n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}
void scSwap(uint addr)
{
  struct proc *proc = myproc();
  int i, j;
  char buf[BUF_SIZE];
  pte_t *pte1, *pte2;
  struct freepg *curr, *oldpgtail;

  if (proc->pghead == 0)
    panic("scSwap: proc->pghead is NULL");
  if (proc->pghead->next == 0)
    panic("scSwap: single page in phys mem");

  curr = proc->pgtail;
  oldpgtail = proc->pgtail; // to avoid infinite loop if somehow everyone was accessed
  do
  {
    //move curr from pgtail to pghead
    proc->pgtail = proc->pgtail->prev;
    proc->pgtail->next = 0;
    curr->prev = 0;
    curr->next = proc->pghead;
    proc->pghead->prev = curr;
    proc->pghead = curr;
    curr = proc->pgtail;
 
  } while (checkAndClearFlag(proc->pghead->va,1,PTE_A) && curr != oldpgtail);
  if((uint)proc->pghead->va <= 0x2000){
    curr = proc->pgtail;
  oldpgtail = proc->pgtail;
    do
  {
    //move curr from pgtail to pghead
    proc->pgtail = proc->pgtail->prev;
    proc->pgtail->next = 0;
    curr->prev = 0;
    curr->next = proc->pghead;
    proc->pghead->prev = curr;
    proc->pghead = curr;
    curr = proc->pgtail;
  } while (checkAndClearFlag(proc->pghead->va,1,PTE_A) && curr != oldpgtail);
  }
  //find the address of the page table entry to copy into the swap file
  pte1 = walkpgdir(proc->pgdir, (void *)proc->pghead->va, 0);
  if (!*pte1)
    panic("swapFile: SCFIFO pte1 is empty");

  //find a swap file page descriptor slot
  for (i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (proc->swappedpages[i].va == (char *)PTE_ADDR(addr))
      goto foundswappedpageslot;
  }
  panic("scSwap: SCFIFO no slot for swapped page");

foundswappedpageslot:
  //cprintf("csSwap swaploc %d\n",proc->swappedpages[i].swaploc);
  proc->swappedpages[i].va = proc->pghead->va;
  //assign the physical page to addr in the relevant page table
  pte2 = walkpgdir(proc->pgdir, (void *)addr, 0);
  if (!*pte2)
    panic("swapFile: SCFIFO pte2 is empty");
  //set page table entry
  //TODO verify we're not setting PTE_U where we shouldn't be...
  *pte2 = PTE_ADDR(*pte1) | PTE_U | PTE_W | PTE_P; // access bit is zeroed...

  for (j = 0; j < 4; j++)
  {
    int loc = (i * PGSIZE) + ((PGSIZE / 4) * j);
    // cprintf("i:%d j:%d loc:0x%x\n", i,j,loc);//TODO delete
    int addroffset = ((PGSIZE / 4) * j);
    // int read, written;
    memset(buf, 0, BUF_SIZE);
    //copy the new page from the swap file to buf
    // read =
    readFromSwapFile(proc, buf, loc, BUF_SIZE);
    // cprintf("read:%d\n", read);//TODO delete
    //copy the old page from the memory to the swap file
    //written =
    writeToSwapFile(proc, (char *)(P2V_WO(PTE_ADDR(*pte1)) + addroffset), loc, BUF_SIZE);
    // cprintf("written:%d\n", written);//TODO delete
    //copy the new page from buf to the memory
    memmove((void *)(PTE_ADDR(addr) + addroffset), (void *)buf, BUF_SIZE);
  }
  //update the page table entry flags, reset the physical page address
  *pte1 = PTE_U | PTE_W | PTE_PG;
  //update l to hold the new va
  //l->next = proc->pghead;
  //proc->pghead = l;
  proc->pghead->va = (char *)PTE_ADDR(addr);
}
void aqSwap(uint addr)
{
  struct proc *proc = myproc();
  int i, j;
  char buf[BUF_SIZE];
  pte_t *pte1, *pte2;
  struct freepg *curr;

  if (proc->pghead == 0)
    panic("aqSwap: proc->pghead is NULL");
  if (proc->pghead->next == 0)
    panic("aqSwap: single page in phys mem");

do{
  curr = proc->pgtail;
  //move curr from pgtail to pghead
  proc->pgtail = proc->pgtail->prev;
  proc->pgtail->next = 0;
  curr->prev = 0;
  curr->next = proc->pghead;
  proc->pghead->prev = curr;
  proc->pghead = curr;
}while((uint)proc->pghead->va <= 0x2000);//swap only user private pages

  pte1 = walkpgdir(proc->pgdir, (void *)proc->pghead->va, 0);
  if (!*pte1)
    panic("swapFile: AQ pte1 is empty");

  //find a swap file page descriptor slot
  for (i = 0; i < MAX_PSYC_PAGES; i++)
  {
    if (proc->swappedpages[i].va == (char *)PTE_ADDR(addr))
      goto foundswappedpageslot;
  }
  cprintf("%x before panic \n",(uint)PTE_ADDR(addr));
  panic("scSwap: AQ no slot for swapped page");

foundswappedpageslot:

  proc->swappedpages[i].va = proc->pghead->va;
  //assign the physical page to addr in the relevant page table
  pte2 = walkpgdir(proc->pgdir, (void *)addr, 0);
  if (!*pte2)
    panic("swapFile: AQ pte2 is empty");
  //set page table entry
  //TODO verify we're not setting PTE_U where we shouldn't be...
  *pte2 = PTE_ADDR(*pte1) | PTE_U | PTE_W | PTE_P; // access bit is zeroed...

  for (j = 0; j < 4; j++)
  {
    int loc = (i * PGSIZE) + ((PGSIZE / 4) * j);
    // cprintf("i:%d j:%d loc:0x%x\n", i,j,loc);//TODO delete
    int addroffset = ((PGSIZE / 4) * j);
    // int read, written;
    memset(buf, 0, BUF_SIZE);
    //copy the new page from the swap file to buf
    // read =
    readFromSwapFile(proc, buf, loc, BUF_SIZE);
    // cprintf("read:%d\n", read);//TODO delete
    //copy the old page from the memory to the swap file
    //written =
    writeToSwapFile(proc, (char *)(P2V_WO(PTE_ADDR(*pte1)) + addroffset), loc, BUF_SIZE);
    // cprintf("written:%d\n", written);//TODO delete
    //copy the new page from buf to the memory
    memmove((void *)(PTE_ADDR(addr) + addroffset), (void *)buf, BUF_SIZE);
  }
  //update the page table entry flags, reset the physical page address
  *pte1 = PTE_U | PTE_W | PTE_PG;
  //update l to hold the new va
  //l->next = proc->pghead;
  //proc->pghead = l;
  proc->pghead->va = (char *)PTE_ADDR(addr);
}

void handlePageFault(uint addr)
{
  struct proc *proc = myproc();
  if (strcmp(proc->name, "init") == 0 || strcmp(proc->name, "sh") == 0)
  {
    proc->pagesInRAM++;
    return;
  }
#ifdef SCFIFO
  scSwap(addr);
#else
#ifdef AQ
  aqSwap(addr);
#else
#ifdef NFUA
    nfuaSwap(addr);
#else
#ifdef LAPA
    lapaSwap(addr);
#endif
#endif
#endif
#endif

  lcr3(V2P(proc->pgdir));
  ++proc->totalPagedOut;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

