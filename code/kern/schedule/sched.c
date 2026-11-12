#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2410826

// schedule - pick the next runnable thread and switch to it.
void
schedule(void) {
    bool intr_flag;
    list_entry_t *le, *last;
    struct proc_struct *next = NULL;

    local_intr_save(intr_flag);
    {
        current->need_resched = 0;

        // For a normal thread start right after itself; for the idle thread,
        // which is not linked into proc_list, start from the list head so
        // that every thread gets a fair chance.
        last = (current == idleproc) ? &proc_list : &(current->list_link);
        le = last;
        do {
            le = list_next(le);
            if (le != &proc_list) {
                next = le2proc(le, list_link);
                if (next->state == PROC_RUNNABLE) {
                    break;
                }
                next = NULL;
            }
        } while (le != last);

        // No runnable thread found in a full round: fall back to idleproc.
        if (next == NULL) {
            next = idleproc;
        }

        next->runs++;
        if (next != current) {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}


