#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <../sync/sync.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    //pmm_manager = &default_pmm_manager;
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
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
void free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
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

/* Bounds derived from the fixed boot mapping.  They are defined locally so
 * that this file works whether or not memlayout.h provides them. */
#ifndef KERNEL_BEGIN_PADDR
#define KERNEL_BEGIN_PADDR  (KERNBASE - PHYSICAL_MEMORY_OFFSET)
#endif
#ifndef PHYSICAL_MEMORY_END
#define PHYSICAL_MEMORY_END (KERNTOP - PHYSICAL_MEMORY_OFFSET)
#endif

static void page_init(void) {
    extern char end[];  // virtual address just past the kernel image

    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uint64_t probe_end;       // end of the range reported by the DTB
    uintptr_t mem_end;        // page-aligned end of the managed range
    uintptr_t kernel_end_pa;  // physical address of the kernel end
    uintptr_t pages_pa;       // physical address of the descriptor array
    uintptr_t free_start_pa;  // first complete free page
    size_t nr_descs;          // number of descriptors = npage - nbase
    size_t desc_size;         // bytes occupied by the descriptor array
    size_t free_page_count;   // number of complete free pages
    size_t i;

    /* The boot page table maps [DRAM_BASE, DRAM_BASE + 1GiB) with this fixed
     * offset; it is not derived from the memory base reported by the DTB. */
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    /* Validate the DTB information before any address arithmetic or access. */
    if (mem_base == 0 || mem_size == 0) {
        panic("page_init: invalid memory info (base=0x%016lx, size=0x%016lx)",
              (unsigned long)mem_base, (unsigned long)mem_size);
    }
    if (mem_base < DRAM_BASE) {
        panic("page_init: memory base 0x%016lx is below DRAM_BASE 0x%016lx",
              (unsigned long)mem_base, (unsigned long)DRAM_BASE);
    }
    if (mem_size > ~(uint64_t)0 - mem_base) {
        panic("page_init: memory range overflow (base=0x%016lx, size=0x%016lx)",
              (unsigned long)mem_base, (unsigned long)mem_size);
    }
    probe_end = mem_base + mem_size;

    /* The kernel image must start inside the reported memory. */
    if (mem_base > (uint64_t)KERNEL_BEGIN_PADDR) {
        panic("page_init: memory base 0x%016lx is above kernel image 0x%016lx",
              (unsigned long)mem_base, (unsigned long)KERNEL_BEGIN_PADDR);
    }

    /* Never manage memory above the fixed upper bound; the managed end is
     * rounded down so that only complete pages are included. */
    mem_end = (uintptr_t)probe_end;
    if (mem_end > (uintptr_t)PHYSICAL_MEMORY_END) {
        mem_end = (uintptr_t)PHYSICAL_MEMORY_END;
    }
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
    if (mem_end <= (uintptr_t)mem_base) {
        panic("page_init: no usable physical memory in [0x%016lx, 0x%016lx)",
              (unsigned long)mem_base, (unsigned long)probe_end);
    }

    /* npage is the upper bound of physical page numbers, not the number of
     * free pages.  pages[0] still describes PPN nbase. */
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: npage %lu is not above nbase %lu",
              (unsigned long)npage, (unsigned long)nbase);
    }

    /* The kernel must end below the managed end, otherwise the descriptor
     * array placed after it cannot fit in valid memory. */
    kernel_end_pa = PADDR((uintptr_t)end);
    if (kernel_end_pa >= mem_end) {
        panic("page_init: kernel end 0x%016lx is not below managed end 0x%016lx",
              (unsigned long)kernel_end_pa, (unsigned long)mem_end);
    }

    /* Place the descriptor array on the first page after the kernel image. */
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    pages_pa = PADDR((uintptr_t)pages);

    /* Check that the whole array fits below the managed end before writing. */
    nr_descs = npage - nbase;
    if (nr_descs > ~(size_t)0 / sizeof(struct Page)) {
        panic("page_init: descriptor size overflow (count=%lu)",
              (unsigned long)nr_descs);
    }
    desc_size = nr_descs * sizeof(struct Page);
    if (desc_size > mem_end || pages_pa > mem_end - desc_size) {
        panic("page_init: page descriptors [0x%016lx, +0x%lx) do not fit below 0x%016lx",
              (unsigned long)pages_pa, (unsigned long)desc_size,
              (unsigned long)mem_end);
    }

    /* Descriptor memory is not guaranteed to be zero: clear it explicitly.
     * Every page starts reserved and is only released through init_memmap. */
    memset(pages, 0, desc_size);
    for (i = 0; i < nr_descs; i++) {
        SetPageReserved(&pages[i]);
    }

    /* First complete page after the kernel image and descriptor array. */
    free_start_pa = (uintptr_t)ROUNDUP(pages_pa + desc_size, PGSIZE);
    if (free_start_pa < (uintptr_t)ROUNDUP(mem_base, PGSIZE)) {
        free_start_pa = (uintptr_t)ROUNDUP(mem_base, PGSIZE);
    }
    if (free_start_pa < mem_end) {
        free_page_count = (size_t)((mem_end - free_start_pa) / PGSIZE);
    } else {
        free_page_count = 0;
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
            (unsigned long)free_page_count);
    cprintf("pages: 0x%016lx (physical 0x%016lx), %lu descriptors, kernel end 0x%016lx\n",
            (unsigned long)(uintptr_t)pages, (unsigned long)pages_pa,
            (unsigned long)nr_descs, (unsigned long)kernel_end_pa);

    /* Only complete pages are registered; an empty range is Case 3. */
    if (free_page_count > 0) {
        init_memmap(&pages[PPN(free_start_pa) - nbase], free_page_count);
    }
}


/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
