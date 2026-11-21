#include <default_pmm.h>
#include <defs.h>
#include <error.h>
#include <kmalloc.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <dtb.h>
#include <stdio.h>
#include <string.h>
#include <sync.h>
#include <vmm.h>
#include <riscv.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// The kernel image is mapped at VA=KERNBASE and PA=info.base
uint_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
pde_t *boot_pgdir_va = NULL;
// physical address of boot-time page directory
uintptr_t boot_pgdir_pa;

// physical memory management
const struct pmm_manager *pmm_manager;

static void check_alloc_page(void);
static void check_pgdir(void);
static void check_boot_pgdir(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void)
{
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n)
{
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n)
{
    struct Page *page = NULL;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        page = pmm_manager->alloc_pages(n);
    }
    local_intr_restore(intr_flag);
    return page;
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void)
{
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

//lab2 请补充你在lab2中的代码
//lab2 function1 2412084
/* This tree's memlayout.h does not define PHYSICAL_MEMORY_END, so derive the
 * same upper bound of the managed physical memory from the fixed boot
 * mapping: KERNTOP is the virtual address of physical address 0x88000000. */
#ifndef PHYSICAL_MEMORY_END
#define PHYSICAL_MEMORY_END ((uintptr_t)(KERNTOP - PHYSICAL_MEMORY_OFFSET))
#endif

/* page_init - detect physical memory from the device tree and initialize the
 * page descriptor array.
 *
 * Physical layout after this function returns (all boundaries page aligned):
 *
 *   DRAM_BASE      kernel image end      free_start_pa             mem_end
 *   | OpenSBI ...  | kernel | Page array |     free pages        |
 *   |<------------------- reserved ----->|<--- init_memmap ----->|
 *
 * pages[0] describes physical page nbase = DRAM_BASE / PGSIZE, so the
 * descriptor array holds npage - nbase entries; that is not the number of
 * free pages.  Every page starts reserved and only the pages handed to
 * init_memmap stop being reserved.
 */
static void page_init(void)
{
    /* virtual address of the first byte after the kernel image */
    extern char end[];

    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uint64_t probe_end, mem_end;
    uintptr_t kernel_end_pa, pages_pa, free_start_pa;
    size_t nr_descs, desc_size, nr_free_cnt, i;

    /* The boot page table maps physical memory at DRAM_BASE with this fixed
     * virtual/physical offset. */
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    /* Validate the information reported by the device tree before doing any
     * address arithmetic or dereferencing memory. */
    if (mem_base == 0 || mem_size == 0)
    {
        panic("page_init: invalid memory info (base=0x%016lx, size=0x%016lx)",
              (unsigned long)mem_base, (unsigned long)mem_size);
    }
    /* nbase and page2pa/pa2page are anchored at DRAM_BASE, so any other base
     * would make the page number conversion inconsistent with the mapping. */
    if (mem_base != DRAM_BASE)
    {
        panic("page_init: memory base 0x%016lx incompatible with DRAM_BASE 0x%016lx",
              (unsigned long)mem_base, (unsigned long)DRAM_BASE);
    }
    if (mem_size > ~(uint64_t)0 - mem_base)
    {
        panic("page_init: memory range overflow (base=0x%016lx, size=0x%016lx)",
              (unsigned long)mem_base, (unsigned long)mem_size);
    }
    probe_end = mem_base + mem_size;

    /* Never manage memory above the fixed upper bound, and only keep complete
     * pages: the managed end is rounded down to a page boundary. */
    mem_end = probe_end;
    if (mem_end > PHYSICAL_MEMORY_END)
    {
        mem_end = PHYSICAL_MEMORY_END;
    }
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
    if (mem_end <= mem_base)
    {
        panic("page_init: no usable physical memory in [0x%016lx, 0x%016lx)",
              (unsigned long)mem_base, (unsigned long)probe_end);
    }

    /* npage is the upper bound of physical page numbers, not a free count. */
    npage = PPN(mem_end);
    if (npage <= nbase)
    {
        panic("page_init: npage %lu is not above nbase %lu",
              (unsigned long)npage, (unsigned long)nbase);
    }

    /* Place the descriptor array on the first page after the kernel image. */
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    kernel_end_pa = PADDR((uintptr_t)end);
    pages_pa = PADDR((uintptr_t)pages);

    /* The kernel image must already lie inside the managed range, otherwise
     * the descriptor array cannot be placed consistently with the mapping. */
    if (kernel_end_pa < mem_base || kernel_end_pa > mem_end)
    {
        panic("page_init: kernel end pa 0x%016lx outside managed [0x%016lx, 0x%016lx)",
              (unsigned long)kernel_end_pa, (unsigned long)mem_base,
              (unsigned long)mem_end);
    }

    /* Check that the whole descriptor array fits in the managed range before
     * writing to it. */
    nr_descs = npage - nbase;
    if (nr_descs > ~(size_t)0 / sizeof(struct Page))
    {
        panic("page_init: descriptor size overflow (count=%lu)",
              (unsigned long)nr_descs);
    }
    desc_size = nr_descs * sizeof(struct Page);
    if (pages_pa < mem_base || pages_pa > mem_end || desc_size > mem_end - pages_pa)
    {
        panic("page_init: page descriptors [0x%016lx, +0x%lx) do not fit in [0x%016lx, 0x%016lx)",
              (unsigned long)pages_pa, (unsigned long)desc_size,
              (unsigned long)mem_base, (unsigned long)mem_end);
    }

    /* Descriptor memory is not guaranteed to be zeroed, so initialize it
     * explicitly.  Every described page starts reserved; init_memmap is the
     * only place that releases pages to the allocator. */
    memset(pages, 0, desc_size);
    for (i = 0; i < nr_descs; i++)
    {
        SetPageReserved(&pages[i]);
    }

    /* First complete free page after the kernel image and descriptor array.
     * pages_pa + desc_size <= mem_end was checked above, so this rounding
     * cannot overflow or pass the managed end (which is page aligned). */
    free_start_pa = (uintptr_t)ROUNDUP(pages_pa + desc_size, PGSIZE);
    if (free_start_pa < mem_end)
    {
        nr_free_cnt = (size_t)((mem_end - free_start_pa) / PGSIZE);
    }
    else
    {
        nr_free_cnt = 0;
    }

    cprintf("physical memory detected: [0x%016lx, 0x%016lx)\n",
            (unsigned long)mem_base, (unsigned long)probe_end);
    cprintf("physical memory managed:  [0x%016lx, 0x%016lx), npage: %lu\n",
            (unsigned long)mem_base, (unsigned long)mem_end,
            (unsigned long)npage);
    cprintf("reserved: [0x%016lx, 0x%016lx) (firmware+kernel+Page array)\n",
            (unsigned long)DRAM_BASE, (unsigned long)free_start_pa);
    cprintf("free:     [0x%016lx, 0x%016lx), %lu pages\n",
            (unsigned long)free_start_pa, (unsigned long)mem_end,
            (unsigned long)nr_free_cnt);
    cprintf("pages: 0x%016lx (physical 0x%016lx), %lu descriptors\n",
            (unsigned long)(uintptr_t)pages, (unsigned long)pages_pa,
            (unsigned long)nr_descs);

    /* Register the free block only when at least one complete page exists. */
    if (nr_free_cnt > 0)
    {
        init_memmap(&pages[PPN(free_start_pa) - nbase], nr_free_cnt);
    }
}

// boot_map_segment - setup&enable the paging mechanism
// parameters
//  la:   linear address of this memory need to map (after x86 segment map)
//  size: memory size
//  pa:   physical address of this memory
//  perm: permission of this memory
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size,
                             uintptr_t pa, uint32_t perm)
{
    assert(PGOFF(la) == PGOFF(pa));
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    for (; n > 0; n--, la += PGSIZE, pa += PGSIZE)
    {
        pte_t *ptep = get_pte(pgdir, la, 1);
        assert(ptep != NULL);
        *ptep = pte_create(pa >> PGSHIFT, PTE_V | perm);
    }
}

// boot_alloc_page - allocate one page using pmm->alloc_pages(1)
// return value: the kernel virtual address of this allocated page
// note: this function is used to get the memory for PDT(Page Directory
// Table)&PT(Page Table)
static void *boot_alloc_page(void)
{
    struct Page *p = alloc_page();
    if (p == NULL)
    {
        panic("boot_alloc_page failed.\n");
    }
    return page2kva(p);
}

// pmm_init - setup a pmm to manage physical memory, build PDT&PT to setup
// paging mechanism
//         - check the correctness of pmm & paging mechanism, print PDT&PT
void pmm_init(void)
{
    // We need to alloc/free the physical memory (granularity is 4KB or other
    // size).
    // So a framework of physical memory manager (struct pmm_manager)is defined
    // in pmm.h
    // First we should init a physical memory manager(pmm) based on the
    // framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a
    // pmm
    check_alloc_page();

    // create boot_pgdir, an initial page directory(Page Directory Table, PDT)
    extern char boot_page_table_sv39[];
    boot_pgdir_va = (pte_t *)boot_page_table_sv39;
    boot_pgdir_pa = PADDR(boot_pgdir_va);

    check_pgdir();

    static_assert(KERNBASE % PTSIZE == 0 && KERNTOP % PTSIZE == 0);

    // now the basic virtual memory map(see memalyout.h) is established.
    // check the correctness of the basic virtual memory map.
    check_boot_pgdir();

    kmalloc_init();
}

// get_pte - get pte and return the kernel virtual address of this pte for la
//        - if the PT contians this pte didn't exist, alloc a page for PT
// parameter:
//  pgdir:  the kernel virtual base address of PDT
//  la:     the linear address need to map
//  create: a logical value to decide if alloc a page for PT
// return vaule: the kernel virtual address of this pte
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create)
{
    pde_t *pdep1 = &pgdir[PDX1(la)];
    if (!(*pdep1 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep1 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }

    pde_t *pdep0 = &((pde_t *)KADDR(PDE_ADDR(*pdep1)))[PDX0(la)];
    if (!(*pdep0 & PTE_V))
    {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL)
        {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep0 = pte_create(page2ppn(page), PTE_U | PTE_V);
    }
    return &((pte_t *)KADDR(PDE_ADDR(*pdep0)))[PTX(la)];
}

// get_page - get related Page struct for linear address la using PDT pgdir
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep_store != NULL)
    {
        *ptep_store = ptep;
    }
    if (ptep != NULL && *ptep & PTE_V)
    {
        return pte2page(*ptep);
    }
    return NULL;
}

// page_remove_pte - free an Page sturct which is related linear address la
//                - and clean(invalidate) pte which is related linear address la
// note: PT is changed, so the TLB need to be invalidate
static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
{
    if (*ptep & PTE_V)
    {
        struct Page *page = pte2page(*ptep);
        page_ref_dec(page);
        if (page_ref(page) == 0)
        {
            free_page(page);
        }
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
}

void unmap_range(pde_t *pgdir, uintptr_t start, uintptr_t end)
{
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));

    do
    {
        pte_t *ptep = get_pte(pgdir, start, 0);
        if (ptep == NULL)
        {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue;
        }
        if (*ptep != 0)
        {
            page_remove_pte(pgdir, start, ptep);
        }
        start += PGSIZE;
    } while (start != 0 && start < end);
}

//lab5 function2 2412084
// exit_range - reclaim empty intermediate page tables covering [start, end).
// The old version decided whether to free a 1 GiB-level directory from the
// VMA sub-range alone and treated an absent 2 MiB entry as a veto.  Both
// could free a directory that still had children or leak tables after a
// partial construction failure.  The whole child table is scanned now.
void exit_range(pde_t *pgdir, uintptr_t start, uintptr_t end)
{
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));

    uintptr_t d1start, d0start;
    pde_t *pd0, *pt, pde1, pde0;
    int i, free_pt, free_pd0;

    d1start = ROUNDDOWN(start, PDSIZE);
    d0start = ROUNDDOWN(start, PTSIZE);
    do
    {
        pde1 = pgdir[PDX1(d1start)];
        /* Only a valid non-leaf entry points to a page directory; a 1 GiB
         * leaf is a data mapping and must not be dereferenced or freed. */
        if ((pde1 & PTE_V) && (pde1 & (PTE_R | PTE_W | PTE_X)) == 0)
        {
            pd0 = page2kva(pde2page(pde1));
            if (d0start < d1start)
            {
                d0start = d1start;
            }
            for (; d0start < d1start + PDSIZE && d0start < end; d0start += PTSIZE)
            {
                pde0 = pd0[PDX0(d0start)];
                /* Only a valid non-leaf entry points to a page table. */
                if ((pde0 & PTE_V) && (pde0 & (PTE_R | PTE_W | PTE_X)) == 0)
                {
                    pt = page2kva(pde2page(pde0));
                    free_pt = 1;
                    for (i = 0; i < NPTEENTRY; i++)
                    {
                        if (pt[i] & PTE_V)
                        {
                            free_pt = 0;
                            break;
                        }
                    }
                    if (free_pt)
                    {
                        free_page(pde2page(pde0));
                        pd0[PDX0(d0start)] = 0;
                    }
                }
            }

            /* Free the 1 GiB directory only when every one of its 512
             * children is gone.  An invalid child simply has no table and
             * must not veto the decision; a valid child (including a leaf)
             * keeps the directory alive. */
            free_pd0 = 1;
            for (i = 0; i < NPTEENTRY; i++)
            {
                if (pd0[i] & PTE_V)
                {
                    free_pd0 = 0;
                    break;
                }
            }
            if (free_pd0)
            {
                free_page(pde2page(pde1));
                pgdir[PDX1(d1start)] = 0;
            }
        }
        d1start += PDSIZE;
        d0start = d1start;
    } while (d1start != 0 && d1start < end);
}


//lab5 function3 YOUR CODE 

//lab5 function1 2412084
// print_pgdir - walk the active Sv39 page table and print its mappings
static uintptr_t
pgdir_sign_extend(uintptr_t va)
{
    /* Sv39 canonical form: sign-extend bit 38 into the upper bits. */
    return (uintptr_t)(((intptr_t)(va << (64 - 39))) >> (64 - 39));
}

static const char *
pgdir_perm_str(pte_t pte)
{
    static char str[5];
    str[0] = (pte & PTE_U) ? 'u' : '-';
    str[1] = (pte & PTE_R) ? 'r' : '-';
    str[2] = (pte & PTE_W) ? 'w' : '-';
    str[3] = (pte & PTE_X) ? 'x' : '-';
    str[4] = '\0';
    return str;
}

static void
pgdir_print_table(pte_t *table, int level, uintptr_t base)
{
    int i;
    for (i = 0; i < NPTEENTRY; i++)
    {
        pte_t pte = table[i];
        uintptr_t va;
        uintptr_t pa;
        uintptr_t size;

        if (!(pte & PTE_V))
        {
            continue;
        }

        va = base + ((uintptr_t)i << (PGSHIFT + level * 9));
        if (level == 2)
        {
            va = pgdir_sign_extend(va);
        }

        /* A leaf has at least one of R/W/X set; otherwise this entry points
         * to the next level table and must not be treated as a data page. */
        if (pte & (PTE_R | PTE_W | PTE_X))
        {
            size = (uintptr_t)1 << (PGSHIFT + level * 9);
            pa = PTE_ADDR(pte);
            /* A mapping must be aligned to the page size of its level:
             * 1 GiB for level 2, 2 MiB for level 1, 4 KiB for level 0. */
            if ((pa & (size - 1)) != 0)
            {
                cprintf("  invalid %s leaf alignment at VA 0x%016lx: "
                        "pa 0x%016lx (need 0x%lx)\n",
                        level == 2 ? "1GiB" : level == 1 ? "2MiB" : "4KiB",
                        (unsigned long)va, (unsigned long)pa,
                        (unsigned long)size);
                continue;
            }
            if ((pte & PTE_W) && !(pte & PTE_R))
            {
                cprintf("  [reserved W-only leaf] ");
            }
            cprintf("%-4s VA 0x%016lx-0x%016lx -> PA 0x%016lx %s\n",
                    level == 2 ? "1GiB" : level == 1 ? "2MiB" : "4KiB",
                    (unsigned long)va, (unsigned long)(va + size - 1),
                    (unsigned long)pa, pgdir_perm_str(pte));
            continue;
        }

        if (level == 0)
        {
            cprintf("  invalid non-leaf entry at VA 0x%016lx\n",
                    (unsigned long)va);
            continue;
        }

        pa = PTE_ADDR(pte);
        if ((pa & (PGSIZE - 1)) != 0 || PPN(pa) < nbase || PPN(pa) >= npage)
        {
            cprintf("  invalid page table pa 0x%016lx at VA 0x%016lx\n",
                    (unsigned long)pa, (unsigned long)va);
            continue;
        }
        pgdir_print_table((pte_t *)KADDR(pa), level - 1, va);
    }
}

void print_pgdir(void)
{
    uintptr_t satp = read_csr(satp);
    uintptr_t mode = satp & SATP64_MODE;
    uintptr_t root_pa;

    cprintf("-------------------- BEGIN --------------------\n");

    if (mode != ((uintptr_t)SATP_MODE_SV39 << 60))
    {
        cprintf("print_pgdir: unsupported satp mode 0x%lx\n",
                (unsigned long)(mode >> 60));
    }
    else
    {
        root_pa = (satp & SATP64_PPN) << PGSHIFT;
        if ((root_pa & (PGSIZE - 1)) != 0 ||
            PPN(root_pa) < nbase || PPN(root_pa) >= npage)
        {
            cprintf("print_pgdir: invalid root page table pa 0x%lx\n",
                    (unsigned long)root_pa);
        }
        else
        {
            cprintf("satp: mode=Sv39 root pa 0x%lx\n", (unsigned long)root_pa);
            pgdir_print_table((pte_t *)KADDR(root_pa), 2, 0);
        }
    }

    cprintf("--------------------- END ---------------------\n");
}


//lab4 请补充你在lab4中的代码
//lab4 function1 2412084
// page_insert - map the virtual page containing la to the physical page page
//               and set the permission bits requested by perm.
// return value: 0 on success, -E_NO_MEM when get_pte cannot provide the pte.
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm)
{
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL)
    {
        return -E_NO_MEM;
    }

    page_ref_inc(page);
    if (*ptep & PTE_V)
    {
        struct Page *p = pte2page(*ptep);
        if (p == page)
        {
            /* Remapping the same page only updates permissions: undo the
             * temporary reference so the page is not freed below. */
            page_ref_dec(page);
        }
        else
        {
            /* Drop the old mapping; page_remove_pte handles the old page's
             * reference count, possible free, pte clearing and TLB flush. */
            page_remove_pte(pgdir, la, ptep);
        }
    }

    /* pte_create() sets PTE_V itself; store a physical page number. */
    *ptep = pte_create(page2ppn(page), perm);
    tlb_invalidate(pgdir, la);
    return 0;
}

// page_remove - remove the mapping of the virtual page containing la
void page_remove(pde_t *pgdir, uintptr_t la)
{
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep != NULL)
    {
        page_remove_pte(pgdir, la, ptep);
    }
}


// invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
void tlb_invalidate(pde_t *pgdir, uintptr_t la)
{
    asm volatile("sfence.vma %0" : : "r"(la));
}

// pgdir_alloc_page - call alloc_page & page_insert functions to
//                  - allocate a page size memory & setup an addr map
//                  - pa<->la with linear address la and the PDT pgdir
struct Page *pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm)
{
    struct Page *page = alloc_page();
    if (page != NULL)
    {
        if (page_insert(pgdir, page, la, perm) != 0)
        {
            free_page(page);
            return NULL;
        }
        // swap_map_swappable(check_mm_struct, la, page, 0);
        page->pra_vaddr = la;
        assert(page_ref(page) == 1);
        // cprintf("get No. %d  page: pra_vaddr %x, pra_link.prev %x,
        // pra_link_next %x in pgdir_alloc_page\n", (page-pages),
        // page->pra_vaddr,page->pra_page_link.prev,
        // page->pra_page_link.next);
    }

    return page;
}

static void check_alloc_page(void)
{
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}

static void check_pgdir(void)
{
    // assert(npage <= KMEMSIZE / PGSIZE);
    // The memory starts at 2GB in RISC-V
    // so npage is always larger than KMEMSIZE / PGSIZE
    size_t nr_free_store;

    nr_free_store = nr_free_pages();

    assert(npage <= KERNTOP / PGSIZE);
    assert(boot_pgdir_va != NULL && (uint32_t)PGOFF(boot_pgdir_va) == 0);
    assert(get_page(boot_pgdir_va, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir_va, p1, 0x0, 0) == 0);

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir_va, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = (pte_t *)KADDR(PDE_ADDR(boot_pgdir_va[0]));
    ptep = (pte_t *)KADDR(PDE_ADDR(ptep[0])) + 1;
    assert(get_pte(boot_pgdir_va, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir_va, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert(boot_pgdir_va[0] & PTE_U);
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir_va, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir_va, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir_va, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir_va, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir_va[0])) == 1);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_pgdir() succeeded!\n");
}

static void check_boot_pgdir(void)
{
    size_t nr_free_store;
    pte_t *ptep;
    int i;

    nr_free_store = nr_free_pages();

    for (i = ROUNDDOWN(KERNBASE, PGSIZE); i < npage * PGSIZE; i += PGSIZE)
    {
        assert((ptep = get_pte(boot_pgdir_va, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);
    }

    assert(boot_pgdir_va[0] == 0);

    struct Page *p;
    p = alloc_page();
    assert(page_insert(boot_pgdir_va, p, 0x100, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir_va, p, 0x100 + PGSIZE, PTE_W | PTE_R) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    pde_t *pd1 = boot_pgdir_va, *pd0 = page2kva(pde2page(boot_pgdir_va[0]));
    free_page(p);
    free_page(pde2page(pd0[0]));
    free_page(pde2page(pd1[0]));
    boot_pgdir_va[0] = 0;
    flush_tlb();

    assert(nr_free_store == nr_free_pages());

    cprintf("check_boot_pgdir() succeeded!\n");
}

// perm2str - use string 'u,r,w,-' to present the permission
static const char *perm2str(int perm)
{
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = 'r';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

// get_pgtable_items - In [left, right] range of PDT or PT, find a continuous
// linear addr space
//                  - (left_store*X_SIZE~right_store*X_SIZE) for PDT or PT
//                  - X_SIZE=PTSIZE=4M, if PDT; X_SIZE=PGSIZE=4K, if PT
// paramemters:
//  left:        no use ???
//  right:       the high side of table's range
//  start:       the low side of table's range
//  table:       the beginning addr of table
//  left_store:  the pointer of the high side of table's next range
//  right_store: the pointer of the low side of table's next range
//  return value: 0 - not a invalid item range, perm - a valid item range with
//  perm permission
static int get_pgtable_items(size_t left, size_t right, size_t start,
                             uintptr_t *table, size_t *left_store,
                             size_t *right_store)
{
    if (start >= right)
    {
        return 0;
    }
    while (start < right && !(table[start] & PTE_V))
    {
        start++;
    }
    if (start < right)
    {
        if (left_store != NULL)
        {
            *left_store = start;
        }
        int perm = (table[start++] & PTE_USER);
        while (start < right && (table[start] & PTE_USER) == perm)
        {
            start++;
        }
        if (right_store != NULL)
        {
            *right_store = start;
        }
        return perm;
    }
    return 0;
}
