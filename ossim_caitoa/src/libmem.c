/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

/* Since the spec file noted that any function in user-space or middle floor must not access pcb_t in
kernel directly, we write one helper function (get_pcb_by_pid) to assign pcb_t and work with pcb indirectly
*/

// Implement static helper function to traverse pcb in userspace method
static struct pcb_t *get_pcb_by_pid(struct krnl_t *krnl, uint32_t pid)
{
  if (!krnl || !krnl->running_list->size) return NULL;

  for (int i = 0; i < krnl->running_list->size; i++) {
    if (krnl->running_list->proc[i] != NULL && krnl->running_list->proc[i]->pid == pid) {
      return krnl->running_list->proc[i];
    }
  }
  return NULL;
}

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}


//  MIDDLEWARE FUNCTION
/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);

  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_area_struct *cur_vma = get_vma_by_num(real_pcb->mm, vmaid);
  if (cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  int inc_sz = 0;

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (real_pcb->mm->symrgtbl[rgid].rg_start < real_pcb->mm->symrgtbl[rgid].rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct rgnode;
  if (get_free_vmrg_area(real_pcb, vmaid, size, &rgnode) == 0) {
    real_pcb->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    real_pcb->mm->symrgtbl[rgid].rg_end   = rgnode.rg_end;
    *alloc_addr = rgnode.rg_start;
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
#ifdef MM64
  inc_sz = (uint32_t)(size / (int)PAGING64_PAGESZ) + 1;
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif

  int old_sbrk = cur_vma->sbrk;

  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
#ifdef MM64
  regs.a3 = size;
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
  int call_result = _syscall(caller->krnl, caller->pid, 17, &regs);
  if (call_result != 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  real_pcb->mm->symrgtbl[rgid].rg_start = old_sbrk;
  real_pcb->mm->symrgtbl[rgid].rg_end   = old_sbrk + size;
  *alloc_addr = old_sbrk;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}


// MIDDLEWARE FUNCTION
/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(real_pcb->mm, rgid);
  if (rgnode->rg_start == 0 && rgnode->rg_end == 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  if (freerg_node == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end   = rgnode->rg_end;
  freerg_node->rg_next  = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next  = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(real_pcb->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


// USER-SPACE FUNCTION
/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);   // call middleware here
  if (val == -1) {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("[ALLOC] pid=%d, reg=%d, size=%d, addr=%08x\n", proc->pid, reg_index, size, addr);

#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

// USER-SPACE FUNCTION
/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1) return -1;

#ifdef IODUMP
  printf("%s:%d\n", __func__, __LINE__);
  printf("[FREE] pid=%d, reg=%d\n", proc->pid, reg_index);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif
  return 0;
}


// KERNEL FUNCTION
/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{

  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte)) { 
    /* Page is not online, make it actively living */
    struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
    if (real_pcb == NULL) return -1;
    addr_t vicpgn, swpfpn;
    addr_t vicfpn;
    //    addr_t vicpte;
    // struct sc_regs regs;

    /* TODO Initialize the target frame storing our variable */
    addr_t tgtfpn;

    /* TODO: Play with your paging theory here */

    /* Find victim page */
    if (find_victim_page(real_pcb->mm, &vicpgn) == -1) return -1;

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1) return -1;


    /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/

    /* TODO copy victim frame to swap 
     * SWP(vicfpn <--> swpfpn)
     * SYSCALL 1 sys_memmap
     */
    uint32_t vicpte = pte_get_entry(caller, vicpgn);
    vicfpn = PAGING_FPN(vicpte);

    // Swap between RAM and SWAP
    struct sc_regs regs;
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = vicfpn;    // source frame trong RAM
    regs.a3 = swpfpn;    // dest frame trong SWAP
    _syscall(caller->krnl, caller->pid, 17, &regs);

    /* Update page table */
    pte_set_swap(caller, vicpgn, 0, swpfpn);

    /* Update its online status of the target page */
    // pte_set_fpn(caller, vicpgn, vicfpn);

    /* Swap frame RAM victim to load page*/
    tgtfpn = vicfpn;

    /* Copy page from SWAP into RAM frame
     */
    addr_t tgtswpfpn = PAGING_SWP(pte);
    __swap_cp_page(caller->krnl->active_mswp, tgtswpfpn, caller->krnl->mram, tgtfpn);

    /* Free swap frame with free list của swap */
    MEMPHY_put_freefp(caller->krnl->active_mswp, tgtswpfpn);

    /* Update PTE of destination page — mark online with new fpn */
    pte_set_fpn(caller, pgn, tgtfpn);
    enlist_pgn_node(&real_pcb->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));
  return 0;
}


// KERNEL FUNCTION
/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to access
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  // calculate physical addr
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  /* TODO 
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ 
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;
  regs.a3 = 0;
  _syscall(caller->krnl, caller->pid, 17, &regs); // result will be stored at regs.a3

  *data = (BYTE) regs.a3;
  return 0; 
}


// KERNEL FUNCTION
/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to access
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
  /* TODO 
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE 
   * SYSCALL 17 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = value;
  _syscall(caller->krnl, caller->pid, 17, &regs); 

  return 0;
}


// MIDDLEWARE FUNCTION
/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) return -1;

  struct vm_rg_struct *currg  = get_symrg_byid(real_pcb->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(real_pcb->mm, vmaid);

  /* TODO Invalid memory identify */
  if (currg == NULL || cur_vma == NULL) return -1;
  if (currg->rg_start == 0 && currg->rg_end == 0) return -1;
  if (currg->rg_start + offset >= currg->rg_end) return -1;

  return pg_getval(real_pcb->mm, currg->rg_start + offset, data, caller);

  // return 0;
}


// USER-SPACE FUNCTION
/*libread - PAGING-based read a region memory */
int libread(struct pcb_t *proc, uint32_t source, addr_t offset, uint32_t *destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);
  if (val == -1) return -1;

  *destination = data;
#ifdef IODUMP
  printf("%s:%d\n", __func__, __LINE__); 
  printf("[READ] pid=%d, reg=%d, offset=%d, data=%d\n", proc->pid, source, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif
  return val;
}


// MIDDLEWARE FUNCTION
/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);

  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *currg  = get_symrg_byid(real_pcb->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(real_pcb->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  if (currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pg_setval(real_pcb->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


// USER-SPACE FUNCTION
/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset) {

  int val = __write(proc, 0, destination, offset, data);
  if (val == -1) {
    return -1;
  }

#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("[WRITE] pid=%d, reg=%d, offset=%d, data=%d\n", proc->pid, destination, offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}


// USER-SPACE FUNCTION
/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  if (caller == NULL || size == 0) {
    return -1;
  }
  if (reg_index >= 10) {
    return -1;
  }
  addr_t  addr;
  int val = __kmalloc(caller, -1, reg_index, size, &addr);

  /* TODO: provide OS kmem allocation validation
   */
  if (val != 0) {
    return -1;
  }
  caller->regs[reg_index] = addr;
  return 0;
}


// KERNEL FUNCTION
/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */

  struct krnl_t *krnl = caller->krnl;
  struct mm_struct *mm = krnl->mm;

  if (mm == NULL || size == 0) {
    return -1;
  }
  pthread_mutex_lock(&mmvm_lock);

  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);
  if (cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  addr_t alloc_start;
  // find free region in vm_area first 
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0) {
    // mm->symrgtbl->rg_start = rgnode.rg_start;
    // mm->symrgtbl->rg_end = rgnode.rg_end;
    alloc_start = rgnode.rg_start;
  }
  // if cannot find in vm_area, go to krnl_pgd to find
  else {
    int old_sbrk = cur_vma->sbrk;
    struct sc_regs regs;
    regs.a1 = SYSMEM_INC_OP;
    regs.a2 = vmaid;
#ifdef MM64
    regs.a3 = size;
#else
    regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
    _syscall(krnl, caller->pid, 17, &regs);

    // write allocated region into symrgtbl
    mm->symrgtbl[rgid].rg_start = old_sbrk;
    mm->symrgtbl[rgid].rg_end = old_sbrk + size;
    // update alloc_start
    alloc_start = old_sbrk;
  }

  // calculate the number of page to map
#ifdef MM64
  int pgnum = (int)DIV_ROUND_UP(size, PAGING64_PAGESZ);
#else
  int pgnum = (int)DIV_ROUND_UP(size, PAGING_PAGESZ);
#endif

  // Allocate physical frame & update in krnl->pgd
#ifdef MM64
  for (int i = 0; i < pgnum; i++) {
    addr_t fpn;

    /* Fetch 1 physical frame from RAM */
    if (MEMPHY_get_freefp(krnl->mram, &fpn) != 0) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1; /* run out of RAM */
    }
    // calculate page number of virtual address
    addr_t virtual_addr = alloc_start + (addr_t)i * PAGING64_PAGESZ;
    addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;

    // extract address into 5 versions for hierachical table
    get_pd_from_address(virtual_addr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

    addr_t pgn = virtual_addr/ PAGING64_PAGESZ;

    // init pte entry to hierachical table
    addr_t pteval = 0;
    init_pte(&pteval, 1, fpn, 0, 0, 0, 0);
    // set entry point for virtual address in memory
    pte_set_entry(caller, pgn, pteval);
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
  }

#else
  for (int i = 0; i < pgnum; i++) 
  {
    addr_t fpn;
    addr_t pgn = (alloc_start / PAGING_PAGESZ) + i;

    if (MEMPHY_get_freefp(krnl->mram, &fpn) != 0) {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }

    addr_t pte_val = 0;
    init_pte(&pte_val, 1, fpn, 0, 0, 0, 0);

    pte_set_entry(caller, pgn, pte_val);
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
  }
#endif
  //krnl->symrgtbl...
  //krnl->krnl_pgd ...
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


// USER-SPACE FUNCTION
/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  /* TODO: provide OS level management */
  if (caller == NULL || size == 0 || align == 0 || align > size) {
    return -1;
  }
  struct krnl_t *krnl = caller->krnl;
  struct mm_struct *mm = krnl->mm;

  // allocate memory with size "size" in kernel. Use a temporary rgid to avoid modifying user process
  addr_t storage_addr;
  int krnl_rgid = (int)(PAGING_MAX_SYMTBL_SZ - 1 - cache_pool_id);

  if (krnl_rgid < 0) {
    return -1;
  }
  if (__kmalloc(caller, 0, krnl_rgid, (addr_t)size, &storage_addr) != 0) {
    return -1;
  }
  // Allocate + Init new kcache_pool_struct
  struct kcache_pool_struct *new_pool = (struct kcache_pool_struct*)malloc(sizeof(struct kcache_pool_struct));

  if (new_pool == NULL) {
    return -1;
  }
  new_pool->pool_id = cache_pool_id;
  new_pool->align = (int) align;
  new_pool->size = (int) size;
  new_pool->storage = 0;
  new_pool->next = NULL;

  // add new_pool to linked list
  pthread_mutex_lock(&mmvm_lock);
  new_pool->next  = mm->kcpooltbl;
  mm->kcpooltbl   = new_pool;
  pthread_mutex_unlock(&mmvm_lock);

  //struct krnl_t *krnl = caller->krnl;
  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...
  return 0;
}


// USER-SPACE FUNCTION
/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  struct krnl_t *krnl = proc->krnl;
  if (krnl->mm->kcpooltbl == NULL) {
    return -1;
  }

  // Important: kcache_pool_struct is a linked list, so we must traverse linked list
  struct kcache_pool_struct *new_pool = krnl->mm->kcpooltbl;
  while (new_pool != NULL && new_pool->pool_id != (int)cache_pool_id) {
    new_pool = new_pool->next;
  }
  if (new_pool == NULL) return -1;

  if (reg_index >= PAGING_MAX_SYMTBL_SZ) {
    return -1;
  }

  addr_t alloc_addr;
  addr_t result = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &alloc_addr);   // vmaid = -1 stands for using krnl_pgd
  if (result != 0) {
    return -1;
  }

  // update alloc address in regs[reg_index]
  proc->regs[reg_index] = alloc_addr;
  //krnl->krnl_pgd ...
  return 0;
}


// MIDDLEWARE FUNCTION
/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */ 
addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
  /* TODO: provide OS level management */
  /* TODO: provide OS level management */
  
  struct krnl_t *krnl = caller->krnl;
  if (krnl->mm->kcpooltbl== NULL) {
    return -1;
  }

  struct kcache_pool_struct *new_pool = krnl->mm->kcpooltbl;
  while (new_pool != NULL && new_pool->pool_id != cache_pool_id) {
    new_pool = new_pool->next;
  }
  if (new_pool == NULL) return -1;
  // since we have known new_pool, we can know the size needed for allocation in kernel
  addr_t slot_sz = (addr_t) new_pool->size;

#ifdef MM64
  if (new_pool->align > 0) {
    slot_sz = ((slot_sz + new_pool->align - 1) / new_pool->align) * new_pool->align;
  }
#else
  slot_sz = PAGING_PAGE_ALIGNSZ(slot_sz);
#endif

  struct vm_rg_struct rgnode;
  /* still have freerg avalable */
  if (get_free_vmrg_area(caller, 0, (int)slot_sz, &rgnode) == 0) {
    krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    krnl->mm->symrgtbl[rgid].rg_end   = rgnode.rg_end;
    *alloc_addr = rgnode.rg_start;
    return 0;
  }

  /*If no freerg available*/
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, 0);
  if (cur_vma == NULL) {
    return 1;
  }

  // important: Save old_sbrk before call syscall
  addr_t old_sbrk = cur_vma->sbrk;
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = 0;     // vmaid

#ifdef MM64
  regs.a3 = slot_sz;
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(slot_sz);
#endif
  _syscall(krnl, caller->pid, 17, &regs);

  // update symbolic region table here
  krnl->mm->symrgtbl[rgid].rg_start = old_sbrk;
  krnl->mm->symrgtbl[rgid].rg_end   = old_sbrk + slot_sz;
  *alloc_addr = old_sbrk;

  //krnl->symrgtbl...
  //krnl->kcpooltbl...
  //krnl->krnl_pgd ...

  return 0;

}


// USER-SPACE FUNCTION
int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) return -1;
  /* --- Validate source (user region) --- */
  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) return -1;
  
  /* offset + size must be within allocated region */
  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) return -1;

  /*
   * TODO: Map kernel address range
   */
  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) return -1;
  
  
  struct vm_rg_struct *dst_rg = get_symrg_byid(caller->krnl->mm, destination);
  if (dst_rg == NULL || (dst_rg->rg_start == 0 && dst_rg->rg_end == 0)) return -1;
  if ((addr_t)size > dst_rg->rg_end - dst_rg->rg_start) return -1;

  /* --- Copy byte by byte: user -> kernel --- */
  uint32_t i;
  for (i = 0; i < size; i++) {
    BYTE byte;

    /* Read from user space */
    if (__read_user_mem(caller, 0, (int)source, (addr_t)(offset + i), &byte) != 0) {
      return -1;
    }
      
    /* Write into kernel space */
    if (__write_kernel_mem(caller, -1, (int)destination, (addr_t)i, byte) != 0) {
        // pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }
  }

  return 0;
}


// USER-SPACE FUNCTION
int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  // pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *src_rg = get_symrg_byid(caller->krnl->mm, source);
  if (src_rg == NULL || (src_rg->rg_start == 0 && src_rg->rg_end == 0)) {
    return -1;
  } 

  // Validate destination region
  addr_t src_sz = src_rg->rg_end - src_rg->rg_start;
  if ((addr_t)offset + (addr_t)size > src_sz) return -1;
  
  /*
   * TODO: Map kernel address range. Copy byte-by-byte to user
   */
  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) return -1;
  
  uint32_t i;
  for (i = 0; i < size; i++) {
    BYTE byte;

    /* Read from kernel space */
    if (__read_kernel_mem(caller, -1, (int)source, (addr_t)(offset + i), &byte) != 0) {
      // pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }

    /* Write into user space */
    if (__write_user_mem(caller, 0, (int)destination, (addr_t)i, byte) != 0) {
      // pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
  }

  // pthread_mutex_unlock(&mmvm_lock);
  return 0;
  //__read_kernel_mem(...)
  //__write_user_mem(...);

  //return 1;
}


// KERNEL FUNCTION
/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS memory operator for kernel memory region */
  //krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy

  struct krnl_t *krnl = caller->krnl;
  struct vm_rg_struct *currg = get_symrg_byid(krnl->mm, rgid);
  if (currg == 0 || (currg->rg_start == 0 && currg->rg_end == 0)) {
    return -1;
  } 

  // calculate virtual address in kernel
  addr_t virtual_addr = currg->rg_start + offset;
#ifdef MM64
  /* ---- 64-bit: walk 5-level page table ---- */
  addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
  get_pd_from_address(virtual_addr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  /* Use pt_idx to access krnl_pte (leaf level) */
  addr_t pte = krnl->krnl_pt[pt_idx];
  // validate if page at index pte is present or not
  if (!PAGING_PAGE_PRESENT(pte)) {
    return -1;
  }

  int page_off = virtual_addr & (PAGING64_PAGESZ - 1); 
  addr_t fpn = PAGING_FPN(pte);
  addr_t phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + page_off;
#else
    /* ---- 32-bit: flat 1-level page table ---- */
    int pgn = PAGING_PGN(virtual_addr);
    int page_off = PAGING_OFFST(virtual_addr);

    uint32_t pte = krnl->krnl_pgd[pgn];
    // validate if page at index pte is present or not
    if (!PAGING_PAGE_PRESENT(pte)) {
      return -1;
    }
    addr_t fpn = PAGING_FPN(pte);
    addr_t phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + page_off;

#endif


  
  MEMPHY_read(krnl->mram, phyaddr, data);
  return 0;
}


// KERNEL FUNCTION
/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
*/
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS memory operator for kernel memory region */
  //krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy
  struct krnl_t *krnl = caller->krnl;

  struct vm_rg_struct *currg = get_symrg_byid(krnl->mm, rgid);
  if (currg == NULL || (currg->rg_start == 0 && currg->rg_end == 0)) {
    return -1;
  }

  addr_t virtual_addr = currg->rg_start + offset;

#ifdef MM64
    addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
    get_pd_from_address(virtual_addr, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

    addr_t pte = krnl->krnl_pt[pt_idx];
    int off = virtual_addr & (PAGING64_PAGESZ - 1);

#else
    int pgn = PAGING_PGN(virtual_addr);
    int off = PAGING_OFFST(virtual_addr);

    uint32_t pte = krnl->krnl_pgd[pgn];
#endif

  if (!PAGING_PAGE_PRESENT(pte)) {
    return -1;
  }
  
  addr_t fpn = PAGING_FPN(pte);
  addr_t physical_addr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

  MEMPHY_write(krnl->mram, physical_addr, value);
  return 0;
}


// USER-SPACE FUNCTION
/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  pthread_mutex_lock(&mmvm_lock);

  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *currg  = get_symrg_byid(real_pcb->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(real_pcb->mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0) ||
      currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  int result = pg_getval(real_pcb->mm, currg->rg_start + offset, data, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return result;

}


// MIDDLEWARE FUNCTION
/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);

  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *currg  = get_symrg_byid(real_pcb->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(real_pcb->mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0) ||
      currg->rg_start + offset >= currg->rg_end) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  int result = pg_setval(real_pcb->mm, currg->rg_start + offset, value, caller);
  pthread_mutex_unlock(&mmvm_lock);
  return result;

}


// KERNEL / PHYSICAL FUNCTION
/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


// HELPER FUNCTION FOR ALLOC FUNCTIONS
/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }

  //thêm trường hợp danh sách chỉ có đúng 1 trang
  if (pg->pg_next == NULL)
  {
    *retpgn = pg->pgn;
    mm->fifo_pgn = NULL;
    free(pg);
    return 0;
  }

  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  prev->pg_next = NULL;

  free(pg);

  return 0;
}


// MIDDLEWARE
/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg) {
  
  struct pcb_t *real_pcb = get_pcb_by_pid(caller->krnl, caller->pid);
  if (real_pcb == NULL) return -1;

  struct vm_area_struct *cur_vma = get_vma_by_num(real_pcb->mm, vmaid);
  if (cur_vma == NULL) return -1;

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
  if (rgit == NULL) return -1;

  newrg->rg_start = newrg->rg_end = -1;

  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    {
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end   = rgit->rg_start + size;

      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      {
        struct vm_rg_struct *nextrg = rgit->rg_next;
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end   = nextrg->rg_end;
          rgit->rg_next  = nextrg->rg_next;
          free(nextrg);
        }
        else
        {
          rgit->rg_start = rgit->rg_end; /* dummy node size 0 */
          rgit->rg_next  = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next;
    }
  }

  if (newrg->rg_start == -1) {
    return -1;
  }
    
  return 0;
}

// #endif
