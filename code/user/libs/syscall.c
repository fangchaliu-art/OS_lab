#include <defs.h>
#include <unistd.h>
#include <syscall.h>

/* lab5 function1 2412084: fixed-argument interface.
 * a0 carries the number and a1..a5 the arguments.  Passing five explicit
 * slots avoids reading varargs the caller never supplied. */
static inline int
syscall(int64_t num, uint64_t arg0, uint64_t arg1, uint64_t arg2,
        uint64_t arg3, uint64_t arg4) {
    register int64_t a0 asm("a0") = num;
    register uint64_t a1 asm("a1") = arg0;
    register uint64_t a2 asm("a2") = arg1;
    register uint64_t a3 asm("a3") = arg2;
    register uint64_t a4 asm("a4") = arg3;
    register uint64_t a5 asm("a5") = arg4;

    /* a0 is both the number input and the return-value output.  The kernel
     * restores a1..a5 unchanged, so they are modelled as inputs. */
    asm volatile("ecall"
                 : "+r"(a0)
                 : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5)
                 : "memory");

    return (int)a0;
}

int
sys_exit(int64_t error_code) {
    return syscall(SYS_exit, (uint64_t)error_code, 0, 0, 0, 0);
}

int
sys_fork(void) {
    return syscall(SYS_fork, 0, 0, 0, 0, 0);
}

int
sys_wait(int64_t pid, int *store) {
    return syscall(SYS_wait, (uint64_t)pid, (uint64_t)(uintptr_t)store,
                   0, 0, 0);
}

int
sys_yield(void) {
    return syscall(SYS_yield, 0, 0, 0, 0, 0);
}

int
sys_kill(int64_t pid) {
    return syscall(SYS_kill, (uint64_t)pid, 0, 0, 0, 0);
}

int
sys_getpid(void) {
    return syscall(SYS_getpid, 0, 0, 0, 0, 0);
}

int
sys_putc(int64_t c) {
    return syscall(SYS_putc, (uint64_t)c, 0, 0, 0, 0);
}

int
sys_pgdir(void) {
    return syscall(SYS_pgdir, 0, 0, 0, 0, 0);
}

