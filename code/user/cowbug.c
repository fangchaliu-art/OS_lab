//challenge1 2410826: Dirty COW consequence demo (pair with COW_SIMULATE_DIRTYCOW)
#include <stdio.h>
#include <ulib.h>

#define COWBUG_ORIGINAL 0x11223344u
#define COWBUG_WRITTEN 0x55667788u

/* writable global: shared copy-on-write after fork() in a correct kernel */
static volatile unsigned int cowbug_value = COWBUG_ORIGINAL;

int
main(void)
{
    int pid, code = 0;

    pid = fork();
    assert(pid >= 0);
    if (pid == 0)
    {
        cowbug_value = COWBUG_WRITTEN;
        assert(cowbug_value == COWBUG_WRITTEN);
        exit(0);
    }
    assert(waitpid(pid, &code) == 0 && code == 0);

    if (cowbug_value == COWBUG_ORIGINAL)
    {
        cprintf("cowbug: parent still reads 0x%x, COW isolated the page.\n",
                cowbug_value);
        cprintf("cowbug pass.\n");
        return 0;
    }

    cprintf("cowbug: parent reads 0x%x, the child's write reached the "
            "shared page!\n", cowbug_value);
    cprintf("cowbug: Dirty COW consequence simulated by the defect kernel.\n");
    return 1;
}
