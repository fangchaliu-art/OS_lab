#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

//lab4 请补充你在lab4中的代码
//lab4 function2 2412084
static struct proc_struct *alloc_proc(void);
static int init_main(void *arg);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static void forkret(void);
static int user_main(void *arg);

// copy_thread - build the first trapframe of a new thread on its kernel stack
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;

    /* a new thread returns 0 from fork */
    proc->tf->gpr.a0 = 0;

    if (esp == 0)
    {
        /* kernel thread: start with sp just below its trapframe */
        proc->tf->gpr.sp = (uintptr_t)proc->tf;
    }
    else
    {
        proc->tf->gpr.sp = esp;
    }

    /* first scheduling enters forkret -> forkrets(current->tf) -> __trapret */
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)proc->tf;
}

// proc_init - set up the first kernel thread idleproc and create initproc
void proc_init(void)
{
    int i;
    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL)
    {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process++;

    current = idleproc;

    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0)
    {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    if (initproc == NULL)
    {
        panic("cannot find initproc.\n");
    }
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
}

//lab4 function3 2412084
// proc_run - switch the CPU to proc, loading its page table first
void proc_run(struct proc_struct *proc)
{
    if (proc == current)
    {
        return;
    }

    bool intr_flag;
    struct proc_struct *prev = current, *next = proc;
    local_intr_save(intr_flag);
    {
        current = proc;
        lsatp(proc->pgdir);
        flush_tlb();
        switch_to(&(prev->context), &(next->context));
    }
    local_intr_restore(intr_flag);
}

// cpu_idle - the idle thread: schedule whenever a request is pending
void cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched)
        {
            schedule();
        }
    }
}

//lab5 function2 2412084 update alloc_proc
// alloc_proc - allocate a PCB and initialize every field
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->runs = 0;
        proc->kstack = 0;
        proc->need_resched = 0;
        proc->parent = NULL;
        proc->mm = NULL;
        memset(&(proc->context), 0, sizeof(struct context));
        proc->tf = NULL;
        proc->pgdir = boot_pgdir_pa;
        proc->flags = 0;
        memset(proc->name, 0, PROC_NAME_LEN + 1);
        proc->exit_code = 0;
        proc->wait_state = 0;
        proc->cptr = proc->optr = proc->yptr = NULL;
        list_init(&(proc->list_link));
        list_init(&(proc->hash_link));
    }
    return proc;
}


// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}






// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}

//lab5 function2 2412084 update kernel_thread
// kernel_thread - create a kernel thread whose entry is fn(arg)
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;

    memset(&tf, 0, sizeof(struct trapframe));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;

    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

//lab5 function3 2410826 update copy_mm
/* copy_mm - give proc an address space according to clone_flags
 *
 * returns 0 on success, a negative error code on failure.  On failure the
 * caller still owns only the PCB and kernel stack; nothing created here is
 * left behind. */
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    struct mm_struct *mm, *oldmm = current->mm;
    int ret;

    /* kernel thread: keep using the boot page table, no user space */
    if (oldmm == NULL)
    {
        return 0;
    }

    /* CLONE_VM: share the parent's address space, one more reference */
    if (clone_flags & CLONE_VM)
    {
        mm_count_inc(oldmm);
        proc->mm = oldmm;
        proc->pgdir = PADDR(oldmm->pgdir);
        return 0;
    }

    /* normal fork: build an independent copy of the parent's space */
    if ((mm = mm_create()) == NULL)
    {
        return -E_NO_MEM;
    }
    if (setup_pgdir(mm) != 0)
    {
        /* no root page table yet, so no exit_mmap()/put_pgdir() */
        mm_destroy(mm);
        return -E_NO_MEM;
    }

    /* dup_mmap() must not lock again; lock only around it */
    lock_mm(oldmm);
    {
        ret = dup_mmap(mm, oldmm);
    }
    unlock_mm(oldmm);
    if (ret != 0)
    {
        /* mm_count is still 0, so exit_mmap()'s assertion holds */
        exit_mmap(mm);
        put_pgdir(mm);
        mm_destroy(mm);
        return ret;
    }

    /* publish only after the whole copy succeeded */
    mm_count_inc(mm);
    proc->mm = mm;
    proc->pgdir = PADDR(mm->pgdir);
    return 0;
}


//lab5 function2 2412084
/* Validate one PT_LOAD segment and return its page-aligned interval.
 * Returns 1 for a valid non-empty segment, 0 for "skip" (not PT_LOAD or
 * zero memsz), -1 for an invalid segment.  All range arithmetic is done
 * after bounding p_va/p_memsz inside the user address space. */
static int
elf_load_range(const struct proghdr *ph, size_t size,
               uintptr_t *start, uintptr_t *end)
{
    if (ph->p_type != ELF_PT_LOAD)
    {
        return 0;
    }
    if (ph->p_filesz > ph->p_memsz)
    {
        return -1;
    }
    if (ph->p_memsz == 0)
    {
        return 0;
    }
    if (ph->p_offset > size || ph->p_filesz > size - ph->p_offset)
    {
        return -1;
    }
    if (ph->p_va < USERBASE || ph->p_va >= USERTOP ||
        ph->p_memsz > USERTOP - ph->p_va)
    {
        return -1;
    }
    *start = ROUNDDOWN(ph->p_va, PGSIZE);
    *end = ROUNDUP(ph->p_va + ph->p_memsz, PGSIZE);
    return 1;
}

// load_icode - build a new user address space from an ELF64 image
static int
load_icode(unsigned char *binary, size_t size)
{
    struct mm_struct *mm;
    struct Page *page;
    struct elfhdr *elf;
    struct proghdr *ph, *ph_end, *ph1, *ph2;
    struct trapframe *tf;
    uintptr_t stack_bottom;
    int ret = -E_NO_MEM;
    bool entry_in_exec = 0;

    if (current->mm != NULL || current->tf == NULL || binary == NULL)
    {
        return -E_INVAL;
    }
    if (size < sizeof(struct elfhdr))
    {
        return -E_INVAL;
    }

    elf = (struct elfhdr *)binary;
    if (elf->e_magic != ELF_MAGIC || elf->e_elf[0] != 2 /* ELFCLASS64 */ ||
        elf->e_elf[1] != 1 /* ELFDATA2LSB */ || elf->e_machine != 243 /* EM_RISCV */)
    {
        return -E_INVAL;
    }
    if (elf->e_phnum == 0 || elf->e_phentsize != sizeof(struct proghdr) ||
        (elf->e_phoff & 7) != 0 || elf->e_phoff > size ||
        elf->e_phnum > (size - elf->e_phoff) / sizeof(struct proghdr))
    {
        return -E_INVAL;
    }
    if (!USER_ACCESS(elf->e_entry, elf->e_entry + 1))
    {
        return -E_INVAL;
    }

    ph = (struct proghdr *)(binary + elf->e_phoff);
    ph_end = ph + elf->e_phnum;
    stack_bottom = USTACKTOP - USTACKSIZE;

    /* Layout pre-check before allocating anything: mm_map() only detects an
     * overlap when the new start lies inside an existing VMA, so an
     * out-of-order overlapping image could otherwise trigger
     * check_vma_overlap()'s assertion.  Compare every page-aligned segment
     * interval with all later segments and with the whole stack VMA. */
    for (ph1 = ph; ph1 < ph_end; ph1++)
    {
        uintptr_t s1, e1;
        int r1 = elf_load_range(ph1, size, &s1, &e1);
        if (r1 < 0)
        {
            return -E_INVAL;
        }
        if (r1 == 0)
        {
            continue;
        }
        if (s1 < USTACKTOP && stack_bottom < e1)
        {
            return -E_INVAL;
        }
        for (ph2 = ph1 + 1; ph2 < ph_end; ph2++)
        {
            uintptr_t s2, e2;
            int r2 = elf_load_range(ph2, size, &s2, &e2);
            if (r2 < 0)
            {
                return -E_INVAL;
            }
            if (r2 == 1 && s1 < e2 && s2 < e1)
            {
                return -E_INVAL;
            }
        }
    }

    if ((mm = mm_create()) == NULL)
    {
        goto bad_mm;
    }
    if (setup_pgdir(mm) != 0)
    {
        goto bad_pgdir_cleanup_mm;
    }

    ph = (struct proghdr *)(binary + elf->e_phoff);
    for (; ph < ph_end; ph++)
    {
        uint32_t vm_flags = 0;
        uint32_t perm = PTE_U;
        uintptr_t seg_start, seg_end, file_end, addr;
        int r = elf_load_range(ph, size, &seg_start, &seg_end);

        if (r < 0)
        {
            ret = -E_INVAL;
            goto bad_cleanup_mmap;
        }
        if (r == 0)
        {
            continue;
        }

        if (ph->p_flags & ELF_PF_R)
        {
            vm_flags |= VM_READ;
            perm |= PTE_R;
        }
        if (ph->p_flags & ELF_PF_W)
        {
            /* Sv39 forbids W without R */
            vm_flags |= VM_WRITE | VM_READ;
            perm |= PTE_W | PTE_R;
        }
        if (ph->p_flags & ELF_PF_X)
        {
            vm_flags |= VM_EXEC;
            perm |= PTE_X;
        }
        if (vm_flags == 0)
        {
            ret = -E_INVAL;
            goto bad_cleanup_mmap;
        }

        ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL);
        if (ret != 0)
        {
            goto bad_cleanup_mmap;
        }

        file_end = ph->p_va + ph->p_filesz;
        for (addr = seg_start; addr < seg_end; addr += PGSIZE)
        {
            uintptr_t copy_begin, copy_end;
            char *kva;

            if ((page = pgdir_alloc_page(mm->pgdir, addr, perm)) == NULL)
            {
                ret = -E_NO_MEM;
                goto bad_cleanup_mmap;
            }
            kva = page2kva(page);
            memset(kva, 0, PGSIZE);

            copy_begin = addr > ph->p_va ? addr : ph->p_va;
            copy_end = addr + PGSIZE;
            if (copy_end > file_end)
            {
                copy_end = file_end;
            }
            if (copy_begin < copy_end)
            {
                memcpy(kva + (copy_begin - addr),
                       binary + ph->p_offset + (copy_begin - ph->p_va),
                       copy_end - copy_begin);
            }
        }

        if ((ph->p_flags & ELF_PF_X) && elf->e_entry >= ph->p_va &&
            elf->e_entry - ph->p_va < ph->p_memsz)
        {
            entry_in_exec = 1;
        }
    }

    if (!entry_in_exec)
    {
        ret = -E_INVAL;
        goto bad_cleanup_mmap;
    }

    /* user stack: the VMA covers the full range, the bottom guard page is
     * left unmapped because there is no demand paging.  stack_bottom was
     * computed before the layout pre-check. */
    ret = mm_map(mm, stack_bottom, USTACKSIZE, VM_READ | VM_WRITE | VM_STACK, NULL);
    if (ret != 0)
    {
        goto bad_cleanup_mmap;
    }
    for (uintptr_t addr = stack_bottom + PGSIZE; addr < USTACKTOP; addr += PGSIZE)
    {
        if ((page = pgdir_alloc_page(mm->pgdir, addr, PTE_U | PTE_W | PTE_R)) == NULL)
        {
            ret = -E_NO_MEM;
            goto bad_cleanup_mmap;
        }
        memset(page2kva(page), 0, PGSIZE);
    }

    /* construct the user return frame in place: U mode, interrupts enabled
     * again by sret, kernel SUM preserved. */
    tf = current->tf;
    memset(tf, 0, sizeof(struct trapframe));
    tf->epc = elf->e_entry;
    tf->gpr.sp = USTACKTOP;
    tf->status = (read_csr(sstatus) & SSTATUS_SUM) | SSTATUS_SPIE;

    /* publish only after everything succeeded */
    mm_count_inc(mm);
    current->mm = mm;
    current->pgdir = PADDR(mm->pgdir);
    lsatp(current->pgdir);
    flush_tlb();
    asm volatile("fence.i" ::: "memory");
    return 0;

bad_cleanup_mmap:
    exit_mmap(mm);
bad_elf_cleanup_pgdir:
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    return ret;
}

// do_execve - replace the current process image with the given ELF image
int do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
{
    struct mm_struct *mm = current->mm;
    struct Page *image_pages = NULL;
    size_t image_npages = 0, name_len;
    unsigned char *image = binary;
    bool copied = 0;
    char local_name[PROC_NAME_LEN + 1];
    int ret;

    if (name == NULL || binary == NULL || size == 0)
    {
        return -E_INVAL;
    }
    if (len > 0 && ((uintptr_t)name > (uintptr_t)-1 - len ||
                    !user_mem_check(mm, (uintptr_t)name, len, 0)))
    {
        return -E_INVAL;
    }
    if ((uintptr_t)binary > (uintptr_t)-1 - size ||
        !user_mem_check(mm, (uintptr_t)binary, size, 0))
    {
        return -E_INVAL;
    }

    /* save the name in kernel memory before the old mm is torn down */
    name_len = len < PROC_NAME_LEN ? len : PROC_NAME_LEN;
    memset(local_name, 0, sizeof(local_name));
    memcpy(local_name, name, name_len);

    /* if the image lives in the old user address space, move it out first */
    if (mm != NULL)
    {
        image_npages = ROUNDUP(size, PGSIZE) / PGSIZE;
        if ((image_pages = alloc_pages(image_npages)) == NULL)
        {
            return -E_NO_MEM;
        }
        if (!copy_from_user(mm, page2kva(image_pages), binary, size, 0))
        {
            free_pages(image_pages, image_npages);
            return -E_INVAL;
        }
        image = (unsigned char *)page2kva(image_pages);
        copied = 1;
    }

    bool old_mm_removed = (mm != NULL);
    if (mm != NULL)
    {
        /* leave the old address space before releasing its page table */
        lsatp(boot_pgdir_pa);
        flush_tlb();
        current->pgdir = boot_pgdir_pa;
        if (mm_count_dec(mm) == 0)
        {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }

    ret = load_icode(image, size);
    if (copied)
    {
        free_pages(image_pages, image_npages);
    }
    if (ret != 0)
    {
        if (old_mm_removed)
        {
            /* the old user image is gone; cannot return to its epc */
            do_exit(ret);
            panic("do_execve: load_icode failed");
        }
        return ret;
    }
    set_proc_name(current, local_name);
    return 0;
}

// kernel_execve - exec from kernel context, then enter user mode directly
static int
kernel_execve(const char *name, unsigned char *binary, size_t size)
{
    int ret = do_execve(name, strlen(name), binary, size);
    if (ret == 0)
    {
        /* no trap may intervene between the new state and forkrets */
        intr_disable();
        forkrets(current->tf);
        panic("kernel_execve: forkrets returned");
    }
    return ret;
}

// do_wait - wait for one of the current process's children to become a zombie
int do_wait(int pid, int *code_store)
{
    struct proc_struct *proc;
    bool intr_flag, haskid;

    if (code_store != NULL &&
        !user_mem_check(current->mm, (uintptr_t)code_store, sizeof(int), 1))
    {
        return -E_INVAL;
    }
    if (pid < 0)
    {
        return -E_BAD_PROC;
    }

repeat_find:
    haskid = 0;
    proc = NULL;
    if (pid != 0)
    {
        proc = find_proc(pid);
        if (proc != NULL && proc->parent == current)
        {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE)
            {
                goto found;
            }
        }
    }
    else
    {
        for (proc = current->cptr; proc != NULL; proc = proc->optr)
        {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE)
            {
                goto found;
            }
        }
    }

    if (!haskid)
    {
        return -E_BAD_PROC;
    }

    /* sleep until a child changes state, then scan again */
    local_intr_save(intr_flag);
    {
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        schedule();
        if (current->flags & PF_EXITING)
        {
            do_exit(-E_KILLED);
        }
    }
    local_intr_restore(intr_flag);
    goto repeat_find;

found:
    local_intr_save(intr_flag);
    {
        unhash_proc(proc);
        remove_links(proc);
    }
    local_intr_restore(intr_flag);

    if (code_store != NULL)
    {
        *code_store = proc->exit_code;
    }
    put_kstack(proc);
    kfree(proc);
    return 0;
}

// init_main - create the first user process and reap all its descendants
static int
init_main(void *arg)
{
    size_t nr_free_pages_store = nr_free_pages();
    size_t kernel_allocated_store = kallocated();
    size_t kernel_pages_store = kmalloc_pages();

    int pid = kernel_thread(user_main, arg, 0);
    if (pid <= 0)
    {
        panic("create user_main failed.\n");
    }

    while (1)
    {
        int ret = do_wait(0, NULL);
        if (ret == -E_BAD_PROC)
        {
            break;
        }
        if (ret != 0)
        {
            panic("init_main: do_wait failed: %d", ret);
        }
    }

    cprintf("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL);
    assert(nr_process == 2);
    assert(kernel_allocated_store == kallocated());
    /* SLOB may keep released small-object pages in its arena.  The page
     * allocator difference must be fully explained by growth of that
     * retained set; any remaining difference is a real page leak. */
    assert((intptr_t)nr_free_pages_store - (intptr_t)nr_free_pages() ==
           (intptr_t)kmalloc_pages() - (intptr_t)kernel_pages_store);
    cprintf("init check memory pass.\n");

    /* init adopts orphans, so it must not return through
     * kernel_thread_entry's do_exit; keep it in the scheduling loop. */
    while (1)
    {
        schedule();
    }
    return 0;
}


//lab5 function3 2410826
/* do_fork - create a copy of current as a new runnable process
 *
 * clone_flags & CLONE_VM shares the address space (kernel threads and
 * threads); otherwise the user address space is duplicated.  The child is
 * returned to via forkret and sees a0 == 0, while the parent gets its pid. */
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    bool intr_flag;

    if (nr_process >= MAX_PROCESS)
    {
        goto fork_out;
    }

    ret = -E_NO_MEM;
    if ((proc = alloc_proc()) == NULL)
    {
        goto fork_out;
    }
    /* set_links() relies on parent being set already */
    proc->parent = current;

    if (setup_kstack(proc) != 0)
    {
        goto bad_fork_cleanup_proc;
    }
    if ((ret = copy_mm(clone_flags, proc)) != 0)
    {
        goto bad_fork_cleanup_kstack;
    }
    /* build context/tf now that the kernel stack and mm are ready */
    copy_thread(proc, stack, tf);

    /* pid allocation, hashing and linking must be one atomic step */
    local_intr_save(intr_flag);
    {
        proc->pid = get_pid();
        hash_proc(proc);
        set_links(proc);
    }
    local_intr_restore(intr_flag);

    wakeup_proc(proc);
    ret = proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}


//lab5 function4 2412084
/* do_exit - terminate the current process and become a zombie
 *
 * The user address space is released before the zombie state is published,
 * so do_wait() can never free this PCB and kernel stack while this function
 * is still running on that stack.  The PCB and kernel stack stay alive here
 * and are reclaimed by the parent's do_wait(). */
int
do_exit(int error_code)
{
    struct proc_struct *proc;
    bool intr_flag;

    /* idle and init are the schedulers' fallback and the orphan reaper */
    if (current == idleproc || current == initproc)
    {
        panic("do_exit: idleproc/initproc must not exit.\n");
    }

    /* step 1: leave the old page table, then release the mm reference.
     * A shared mm (CLONE_VM) is only detached, never destroyed here. */
    if (current->mm != NULL)
    {
        struct mm_struct *mm = current->mm;

        lsatp(boot_pgdir_pa);
        flush_tlb();
        current->pgdir = boot_pgdir_pa;
        current->mm = NULL;

        if (mm_count_dec(mm) == 0)
        {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
    }

    /* step 2: publish the exit state, hand children to init and wake up
     * waiters in one interrupt-protected region, then never come back */
    local_intr_save(intr_flag);
    {
        current->state = PROC_ZOMBIE;
        current->exit_code = error_code;

        proc = current->parent;
        if (proc != NULL && proc->wait_state == WT_CHILD)
        {
            wakeup_proc(proc);
        }

        while (current->cptr != NULL)
        {
            proc = current->cptr;
            current->cptr = proc->optr;

            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL)
            {
                initproc->cptr->yptr = proc;
            }
            proc->parent = initproc;
            initproc->cptr = proc;

            /* the adopted child may already be a zombie */
            if (proc->state == PROC_ZOMBIE && initproc->wait_state == WT_CHILD)
            {
                wakeup_proc(initproc);
            }
        }

        schedule();
    }
    local_intr_restore(intr_flag);

    panic("do_exit: zombie process %d was scheduled again.\n", current->pid);
    return 0;
}

/* do_yield - mark current as needing rescheduling, the trap return path
 * performs the actual switch */
int
do_yield(void)
{
    current->need_resched = 1;
    return 0;
}

/* do_kill - mark the target as exiting and wake it if it is interruptibly
 * waiting; the target itself runs do_exit(-E_KILLED) later */
int
do_kill(int pid)
{
    struct proc_struct *proc;
    bool intr_flag;
    int ret;

    /* lookup, flag test and update must be one atomic step: do_fork() and
     * do_wait() may change the hash list and process state concurrently */
    local_intr_save(intr_flag);
    {
        proc = find_proc(pid);
        if (proc == NULL)
        {
            ret = -E_INVAL;
        }
        else if (proc->flags & PF_EXITING)
        {
            ret = -E_KILLED;
        }
        else
        {
            proc->flags |= PF_EXITING;
            /* wake only interruptible sleepers; a runnable target keeps
             * running and notices the flag on its own, a zombie must not
             * reach wakeup_proc()'s assertion */
            if (proc != current && proc->state == PROC_SLEEPING &&
                (proc->wait_state & WT_INTERRUPTED))
            {
                wakeup_proc(proc);
            }
            ret = 0;
        }
    }
    local_intr_restore(intr_flag);
    return ret;
}


#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)


//lab5 function2 2412084
// user_main - kernel thread that loads and enters the first user program
static int
user_main(void *arg)
{
    int ret;

    (void)arg;
#ifdef TEST
    ret = KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    ret = KERNEL_EXECVE(hello);
#endif
    panic("user_main: kernel_execve failed: %d", ret);
}



