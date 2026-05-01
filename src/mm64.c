/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#if defined(MM64)

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}


static addr_t* get_or_create_new_table (addr_t* parent_table, int index) {
  if (parent_table[index] == 0 ) { // nếu cần truy cập vào bảng con mà chưa có
    addr_t* new_table = (addr_t*) malloc(512 * sizeof(addr_t));
    memset(new_table,0, 512* sizeof(new_table));
    parent_table[index] = (addr_t) new_table; // ép kiểu địa chỉ về thành addr và lưu lại
  }
  return (addr_t*) parent_table[index];
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page direactories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* TODO: implement the page direactories mapping */

	return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}


/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
//struct krnl_t *krnl = caller->krnl;

  addr_t *pte;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;
  
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;
  if (caller != NULL && caller->mm != NULL ) {
    addr_t* p4d_table = get_or_create_new_table(caller->mm->pgd,pgd);
    addr_t* pud_table = get_or_create_new_table(p4d_table, p4d);
    addr_t* pmd_table = get_or_create_new_table(pud_table,pud);
    addr_t* pt_table  = get_or_create_new_table(pmd_table,pmd);
    pte = &pt_table[pt];
  } else {
      return -1; 
  }
#else
  //pte = &krnl->mm->pgd[pgn];
  if (caller != NULL && caller->mm != NULL && caller->mm->pgd != NULL) {
      pte = &caller->mm->pgd[pgn];
  } else {
      return -1;
  }
#endif
	
  //SETBIT(*pte, PAGING_PTE_PRESENT_MASK); check lại vấn đề bit 30 và 31 có cùng =1 được không
  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK); // thay đổi bit 31 về 0
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
//struct krnl_t *krnl = caller->krnl;

  addr_t *pte;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;
	
#ifdef MM64	
  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;
  // nhớ bật MM_PAGING
  if (caller !=NULL && caller->mm !=NULL) {
    addr_t* p4d_table = get_or_create_new_table(caller->mm->pgd,pgd);
    addr_t* pud_table = get_or_create_new_table(p4d_table, p4d);
    addr_t* pmd_table = get_or_create_new_table(pud_table,pud);
    addr_t* pt_table  = get_or_create_new_table(pmd_table,pmd);
    pte = & pt_table[pt];
  }
  else return -1; 
#else
  // pte = &krnl->mm->pgd[pgn]; memory leak
  if (caller !=NULL && caller->mm!=NULL && caller->mm->pgd !=NULL) 
  pte = &caller->mm->pgd[pgn];
  else return -1;
#endif
  
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK); // thay đổi bit 31, xác nhận đã có physical mem
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK); // thay đổi bit 30 , xác nhận không bị swap xuống ổ cứng

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}


/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
//struct krnl_t *krnl = caller->krnl;
  uint32_t pte = 0;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t	pt=0;
	
  /* TODO Perform multi-level page mapping */
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  //... krnl->mm->pgd
  //... krnl->mm->pt
  //pte = &krnl->mm->pt;	
  // nhớ bật MM_PAGING khi chạy hàm
	if (caller != NULL && caller->mm !=NULL && caller->mm->pgd != NULL) {
    if(caller->mm->pgd[pgd] == 0) return 0; // nếu bảng con chưa được tạo thì trả về 0
    addr_t* p4d_table = (addr_t*) caller->mm->pgd[pgd];
    if(p4d_table[p4d] == 0) return 0;
    addr_t* pud_table = (addr_t*) p4d_table[p4d];
    if(pud_table[pud] == 0) return 0;
    addr_t* pmd_table = (addr_t*) pud_table[pud];
    if(pmd_table[pmd] == 0) return 0;
    addr_t* pt_table  = (addr_t*) pmd_table[pmd];
    return (uint32_t) pt_table[pt];
  }
  return 0;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{ /*
	struct krnl_t *krnl = caller->krnl;
	krnl->mm->pgd[pgn]=pte_val;
	*/
  addr_t pgd=0, p4d=0, pud=0, pmd=0, pt=0;
  get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt);
  if (caller != NULL && caller->mm !=NULL) {
    addr_t* p4d_table = get_or_create_new_table(caller->mm->pgd,pgd);
    addr_t* pud_table = get_or_create_new_table(p4d_table, p4d);
    addr_t* pmd_table = get_or_create_new_table(pud_table,pud);
    addr_t* pt_table  = get_or_create_new_table(pmd_table,pmd);
    pt_table[pt] = (addr_t) pte_val;
    return 0;
  }
	return -1;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
  //int pgit = 0;
  //uint64_t pattern = 0xdeadbeef;
  uint32_t pattern = 0xdeadbeef; // khởi tạo pattern ban đầu
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT; // lấy số trang page number của địa chỉ bắt đầu
  /* TODO memset the page table with given pattern
   */
  for (int pgit =0; pgit < pgnum; pgit ++) {
    pthread_mutex_lock(&caller->mm->mm_lock);
    pte_set_entry(caller, pgn + pgit, pattern);
    pthread_mutex_unlock(&caller->mm->mm_lock);
  }
  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address (ánh xạ pgnum trang virtual mem qua các frames)
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
//struct framephy_struct *fpit;
//int pgit = 0;
//addr_t pgn;
 if (frames == NULL) return -1; //  không xin được frames nào ban đầu để ánh xạ
 struct framephy_struct* frames_traver = frames; // con trỏ để duyệt dslk các frame
 addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT; // dịch phải 12 bit offset lấy page number
  /* TODO: update the rg_end and rg_start of ret_rg 
  //ret_rg->rg_end =  ....
  //ret_rg->rg_start = ...
  //ret_rg->vmaid = ...
  */

  // thiết lập vùng nhớ ảo được ánh xạ sang các frame
  if(ret_rg !=NULL) {
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;
  }

  for (int pgit = 0 ; pgit < pgnum ; pgit ++) {
    if (frames_traver == NULL) {
      break; // trường hợp hết frames không đủ để cấp
    }
    addr_t fpn = frames_traver->fpn; // lấy frame number
    pthread_mutex_lock(&caller->mm->mm_lock); // bật mutex lên tránh race condition
    pte_set_fpn(caller, pgn + pgit, fpn);
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn + pgit);
    pthread_mutex_unlock(&caller->mm->mm_lock);
    frames_traver = frames_traver->fp_next;
  }
  /* TODO map range of frame to address space
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->krnl->mm->pgd,
   *                    caller->krnl->mm->pud...
   *                    ...
   */

  /* Tracking for later page replacement activities (if needed)
   * Enqueue new usage page */
  //enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn64 + pgit);

  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  //addr_t fpn;
  //int pgit;
  //struct framephy_struct *newfp_str = NULL;
  addr_t fpn =0; // khởi tạo số hiệu frame ban đầu
  struct framephy_struct *newfp_str = NULL, *tail = NULL; // khởi tạo frame ban đầu
  /* TODO: allocate the page 
  //caller-> ...
  //frm_lst-> ...
  */
/*
  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    // TODO: allocate the page 
    if (MEMPHY_get_freefp(caller->mram, &fpn) == 0)
    {
      newfp_str->fpn = fpn;
    }
    else
    { // TODO: ERROR CODE of obtaining somes but not enough frames
    }
  }
*/
  
  for (int pgit = 0; pgit < req_pgnum; pgit++)
  {
    // TODO: allocate the page 
    pthread_mutex_lock(&caller->mram->memphy_lock); // bật mutex
    int getfp_result = MEMPHY_get_freefp(caller->mram, &fpn); // trả về 0 nếu lấy đuoc
    pthread_mutex_unlock(&caller->mram->memphy_lock);
    if (getfp_result == 0) // hàm này sẽ gán fpn = sô frame chưa được dùng
    {
      newfp_str = (struct framephy_struct*) malloc(sizeof(struct framephy_struct)); // khởi tạo con trỏ
      newfp_str->fpn = fpn;
      newfp_str->owner = caller->mm;
      newfp_str->fp_next = NULL;
      if (*frm_lst == NULL) {
        *frm_lst = newfp_str;
        tail = newfp_str;
      }
      else {
        tail->fp_next = newfp_str;
        tail = newfp_str;
      }
    }
    else
    { // TODO: ERROR CODE of obtaining somes but not enough frames
      // thu hồi lại số frame đã cấp vì không đủ để cấp cho toàn bộ
      struct framephy_struct* cur = *frm_lst;
      struct framephy_struct* temp;
      while (cur != NULL ) {
        temp = cur->fp_next;
        pthread_mutex_lock(&caller->mram->memphy_lock);
        MEMPHY_put_freefp(caller->mram, cur->fpn);
        pthread_mutex_unlock(&caller->mram->memphy_lock);
        free(cur);
        cur = temp;
      }
      *frm_lst = NULL;
      return -1; // lỗi không đủ bộ nhớ
    }
  }
  

  /* End TODO */
   return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
//int pgnum = incpgnum;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  // ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000)
  {
    return -1;
  }

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
   vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;

  /* Bật khóa bảo vệ cho cả thiết bị nguồn và thiết bị đích, sửa đổi
   */ 
  if ((uintptr_t)mpsrc < (uintptr_t)mpdst) {
    pthread_mutex_lock(&mpsrc->memphy_lock);
    pthread_mutex_lock(&mpdst->memphy_lock);
} else if ((uintptr_t)mpsrc > (uintptr_t)mpdst) {
    pthread_mutex_lock(&mpdst->memphy_lock);
    pthread_mutex_lock(&mpsrc->memphy_lock);
} else {
    /* Nếu nguồn và đích giống nhau (tự copy), chỉ khóa 1 lần */
    pthread_mutex_lock(&mpsrc->memphy_lock);
}
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++) // ban đầu là PAGING_PAGEZ là 32 bit
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }
 /* Mở khóa theo thứ tự ngược lại */
if ((uintptr_t)mpsrc < (uintptr_t)mpdst) {
    pthread_mutex_unlock(&mpdst->memphy_lock);
    pthread_mutex_unlock(&mpsrc->memphy_lock);
} else if ((uintptr_t)mpsrc > (uintptr_t)mpdst) {
    pthread_mutex_unlock(&mpsrc->memphy_lock);
    pthread_mutex_unlock(&mpdst->memphy_lock);
} else {
    pthread_mutex_unlock(&mpsrc->memphy_lock);
}
  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* TODO init page table directory */
   //mm->pgd = ...
   //mm->p4d = ...
   //mm->pud = ...
   //mm->pmd = ...
   //mm->pt = ...

  /* khởi tạo bắt đầu các cấp page directory, nhớ bật MM_PAGING khi chạy hàm này */
  mm->pgd = malloc(512 * sizeof(addr_t));
  memset(mm->pgd,0, 512 * sizeof(addr_t));
  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;  
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  vma0->vm_freerg_list = NULL; // ban đầu chưa có region nào được cấp phát nên gán null
  /* TODO init the first free region of vma0 */
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* TODO update VMA0 next */
  // vma0->next = ...
  vma0->vm_next = NULL; // vùng nhớ tiếp theo chưa có gán null
  /* Point vma owner backward */
  //vma0->vm_mm = mm; 
  vma0->vm_mm = mm; // con trỏ vm_mm trỏ ngược về mm chứa vma0
  /* TODO: update mmap */
  //mm->mmap = ...
  //mm->symrgtbl = ...
  //mm->kcpooltbl
  
  mm->mmap = vma0; // khởi tạo đầu danh sách vùng nhớ là vma0
  memset(mm->symrgtbl, 0, sizeof(mm->symrgtbl));
  mm->fifo_pgn = NULL;
  mm->kcpooltbl = NULL;

  pthread_mutex_init(&mm->mm_lock, NULL); // khởi tạo lock
  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
//addr_t pgn_start;//, pgn_end;
//addr_t pgit;
//struct krnl_t *krnl = caller->krnl;

  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

  /* TODO traverse the page map and dump the page directory entries */
  if (caller != NULL && caller->mm != NULL) {
    addr_t *pgd_table = caller->mm->pgd;
    addr_t *p4d_table = (pgd_table != NULL && pgd_table[pgd] != 0) ? 
                        (addr_t*)pgd_table[pgd] : NULL;
    addr_t *pud_table = (p4d_table != NULL && p4d_table[p4d] != 0) ? 
                        (addr_t*)p4d_table[p4d] : NULL;         
    addr_t *pmd_table = (pud_table != NULL && pud_table[pud] != 0) ? 
                        (addr_t*)pud_table[pud] : NULL;
    printf(" PDG=%p P4g=%p PUD=%p PMD=%p\n",
           (void*)pgd_table,
           (void*)p4d_table,
           (void*)pud_table,
           (void*)pmd_table); 
  }
  return 0;
}

#endif  //def MM64
