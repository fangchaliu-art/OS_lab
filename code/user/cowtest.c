//challenge1 2410826: functional test for copy-on-write fork
#include <stdio.h>
#include <ulib.h>

#define COW_MAGIC 0x12345678
#define COW_CHILD_MAGIC 0x0badf00d
#define COW_CODE_MAGIC 0x5a5a1234
#define COW_CHILDREN 4

/* a writable global: after fork() its page is a COW page */
static volatile int cow_value = COW_MAGIC;

/* a read-only code target used by the code-protection test */
static __noinline int
code_probe(void)
{
    return COW_CODE_MAGIC;
}

static void
cow_reset(void)
{
    cow_value = COW_MAGIC;
}

/* 1. child writes its copy; the parent must still see the original value */
static void
test_child_write(void)
{
    int pid, code = 0;

    cow_reset();
    pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        cow_value = COW_CHILD_MAGIC;
        assert(cow_value == COW_CHILD_MAGIC);
        exit(0);
    }
    assert(waitpid(pid, &code) == 0 && code == 0);
    assert(cow_value == COW_MAGIC);
    cprintf("cowtest: child-write isolation ok.\n");
}

/* 2. parent writes its copy; the child must read the original value */
static void
test_parent_write(void)
{
    int pid, code = 0;

    cow_reset();
    pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        assert(cow_value == COW_MAGIC);
        exit(0);
    }
    cow_value = COW_CHILD_MAGIC;
    assert(cow_value == COW_CHILD_MAGIC);
    assert(waitpid(pid, &code) == 0 && code == 0);
    assert(cow_value == COW_CHILD_MAGIC);
    cow_reset();
    cprintf("cowtest: parent-write isolation ok.\n");
}

/* 3. several children each get their own copy of the same page */
static void
test_many_children(void)
{
    int pids[COW_CHILDREN], i, code;

    cow_reset();
    for (i = 0; i < COW_CHILDREN; i++)
    {
        pids[i] = fork();
        assert(pids[i] >= 0);
        if (pids[i] == 0)
        {
            cow_value = COW_CHILD_MAGIC + i;
            assert(cow_value == COW_CHILD_MAGIC + i);
            exit(0);
        }
    }
    for (i = 0; i < COW_CHILDREN; i++)
    {
        assert(waitpid(pids[i], &code) == 0 && code == 0);
    }
    assert(cow_value == COW_MAGIC);
    cprintf("cowtest: %d-children isolation ok.\n", COW_CHILDREN);
}

/* 4. a child forks a grandchild; both keep independent copies, and the
 *    COW bit must survive the second fork */
static void
test_nested_fork(void)
{
    int pid, code = 0;

    cow_reset();
    pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        int gc, gcode = 0;

        gc = fork();
        assert(gc >= 0);
        if (gc == 0)
        {
            cow_value = COW_CHILD_MAGIC + 2;
            assert(cow_value == COW_CHILD_MAGIC + 2);
            exit(0);
        }
        cow_value = COW_CHILD_MAGIC + 1;
        assert(cow_value == COW_CHILD_MAGIC + 1);
        assert(waitpid(gc, &gcode) == 0 && gcode == 0);
        exit(0);
    }
    assert(waitpid(pid, &code) == 0 && code == 0);
    assert(cow_value == COW_MAGIC);
    cprintf("cowtest: nested-fork isolation ok.\n");
}

/* 5. writing the read-only text page must kill the child, not change code */
static void
test_code_write(void)
{
    int pid, code = 0;
    volatile unsigned char *probe =
        (volatile unsigned char *)(uintptr_t)code_probe;

    assert(code_probe() == COW_CODE_MAGIC);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        *probe = 0;
        /* the store must have faulted and killed us; reaching here fails */
        exit(1);
    }
    assert(waitpid(pid, &code) == 0);
    /* must be terminated by the kernel with -E_KILLED, not exit(1) */
    assert(code == -9);
    assert(code_probe() == COW_CODE_MAGIC);
    cprintf("cowtest: read-only code write killed child (code %d).\n", code);
}

int
main(void)
{
    cprintf("cowtest: starting COW tests.\n");
    test_child_write();
    test_parent_write();
    test_many_children();
    test_nested_fork();
    test_code_write();
    cprintf("cowtest pass.\n");
    return 0;
}
