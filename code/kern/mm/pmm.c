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
static void page_init(void) {
    //内核镜像结束位置的虚拟地址
    extern char end[];

    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uint64_t probe_end, mem_end;
    //kernel_end_pa内核结束位置的物理地址
    //pages_pa描述符数组起始物理地址
    //free_start_pa可分配区域起始物理地址
    //nr_descs描述符数量desc_size
    //描述符数组占用的字节数
    //nr_free_pages_cnt初始可分配页数
    uintptr_t kernel_end_pa, pages_pa, free_start_pa;
    size_t nr_descs, desc_size, nr_free_pages_cnt, i;

    /* The boot page table maps physical memory starting at DRAM_BASE with
     * the fixed virtual/physical offset. */
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    /* Validate the information reported by the device tree before any
     * address arithmetic or memory access. */
     //基址或容量为零意味着内存信息无效
    if (mem_base == 0 || mem_size == 0) {
        panic("page_init: invalid memory info (base=0x%016lx, size=0x%016lx)",
              (unsigned long)mem_base, (unsigned long)mem_size);
    }
    if (mem_base < DRAM_BASE) {
        panic("page_init: memory base 0x%016lx is below DRAM_BASE 0x%016lx",
              (unsigned long)mem_base, (unsigned long)DRAM_BASE);
    }
    //检查加法溢出
    if (mem_size > ~(uint64_t)0 - mem_base) {
        panic("page_init: memory range overflow (base=0x%016lx, size=0x%016lx)",
              (unsigned long)mem_base, (unsigned long)mem_size);
    }
    probe_end = mem_base + mem_size;

    /* The kernel image must lie inside the reported memory, otherwise the
     * descriptor array cannot be placed consistently with the boot mapping. */
    if (mem_base > KERNEL_BEGIN_PADDR) {
        panic("page_init: memory base 0x%016lx is above kernel image 0x%016lx",
              (unsigned long)mem_base, (unsigned long)KERNEL_BEGIN_PADDR);
    }

    /* Never manage memory above the fixed management upper bound; the end
     * of the managed range is rounded down to a complete page. */
    mem_end = probe_end;
    if (mem_end > PHYSICAL_MEMORY_END) {
        mem_end = PHYSICAL_MEMORY_END;
    }
    //结束地址向下对齐到页面边界
    mem_end = ROUNDDOWN(mem_end, PGSIZE);
    if (mem_end <= mem_base || mem_end <= DRAM_BASE) {
        panic("page_init: no usable physical memory in [0x%016lx, 0x%016lx)",
              (unsigned long)mem_base, (unsigned long)probe_end);
    }

    /* npage is the upper bound of physical page numbers, not a free count. */
    //npage 是物理页号的上界，不是 RAM 的页面数量
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: npage %lu is not above nbase %lu",
              (unsigned long)npage, (unsigned long)nbase);
    }

    /* Place the descriptor array on the first page after the kernel image. */
    pages = (struct Page *)ROUNDUP((uintptr_t)end, PGSIZE);
    pages_pa = PADDR((uintptr_t)pages);
    kernel_end_pa = PADDR((uintptr_t)end);

    /* Check that the whole descriptor array fits in the managed range
     * before writing to it. */
    nr_descs = npage - nbase;
    if (nr_descs > ~(size_t)0 / sizeof(struct Page)) {
        panic("page_init: descriptor size overflow (count=%lu)",
              (unsigned long)nr_descs);
    }
    desc_size = nr_descs * sizeof(struct Page);
    //检查整个数组能否放进管理范围
    if (desc_size > mem_end || pages_pa > mem_end - desc_size) {
        panic("page_init: page descriptors [0x%016lx, +0x%lx) do not fit below 0x%016lx",
              (unsigned long)pages_pa, (unsigned long)desc_size, (unsigned long)mem_end);
    }

    /* The descriptor memory is not guaranteed to be zero, so clear it
     * explicitly. Every described page starts reserved and is only handed
     * to the allocator through init_memmap below. */
    memset(pages, 0, desc_size);
    for (i = 0; i < nr_descs; i++) {
        SetPageReserved(&pages[i]);
    }

    /* First complete free page after the kernel and the descriptor array. */
    free_start_pa = (uintptr_t)ROUNDUP(pages_pa + desc_size, PGSIZE);
    if (free_start_pa < mem_base) {
        //再向上按页对齐
        free_start_pa = (uintptr_t)ROUNDUP(mem_base, PGSIZE);
    }

    if (free_start_pa < mem_end) {
        nr_free_pages_cnt = (mem_end - free_start_pa) / PGSIZE;
    } else {
        nr_free_pages_cnt = 0;
    }

    cprintf("physical memory detected: [0x%016lx, 0x%016lx)\n",
            (unsigned long)mem_base, (unsigned long)probe_end);
    cprintf("physical memory managed:  [0x%016lx, 0x%016lx), npage: %lu\n",
            (unsigned long)mem_base, (unsigned long)mem_end, (unsigned long)npage);
    cprintf("reserved: [0x%016lx, 0x%016lx) (firmware+kernel+Page array)\n",
            (unsigned long)DRAM_BASE, (unsigned long)free_start_pa);
    cprintf("free:     [0x%016lx, 0x%016lx), %lu pages\n",
            (unsigned long)free_start_pa, (unsigned long)mem_end, (unsigned long)nr_free_pages_cnt);
    cprintf("pages: 0x%016lx (physical 0x%016lx), %lu descriptors, kernel end 0x%016lx\n",
            (unsigned long)(uintptr_t)pages, (unsigned long)pages_pa, (unsigned long)nr_descs,
            (unsigned long)kernel_end_pa);

    /* Register the free block only when at least one complete page exists. */
    if (nr_free_pages_cnt > 0) {
        init_memmap(&pages[PPN(free_start_pa) - nbase], nr_free_pages_cnt);
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
