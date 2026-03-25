#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{

	struct proc *p = curr_proc();
    if (val == 0)
        return -1;

    uint64 kva = useraddr(p->pagetable, (uint64)val);
    if (kva == 0)
        return -1;

    TimeVal *kval = (TimeVal *)kva;

    uint64 cycle = get_cycle();
    kval->sec  = cycle / CPU_FREQ;
    kval->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

    return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/

int sys_task_info(struct task_info *uinfo)
{
    if (uinfo == 0)
        return -1;

    struct proc *cur = curr_proc();
    uint64 kva = useraddr(cur->pagetable, (uint64)uinfo);
    if (kva == 0)
        return -1;

    struct task_info *info = (struct task_info *)kva;

    switch (cur->state) {
    case RUNNABLE: info->status = Ready;   break;
    case RUNNING:  info->status = Running; break;
    case UNUSED:
    case USED:     info->status = UnInit;  break;
    case ZOMBIE:
    case SLEEPING:
    default:       info->status = Exited;  break;
    }

    for (int i = 0; i < MAX_SYSCALL_NUM; i++)
        info->syscall_times[i] = cur->syscall_times[i];

    uint64 cycle = get_cycle();
    uint64 sec  = cycle / CPU_FREQ;
    uint64 usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
    uint64 now_ms = sec * 1000 + usec / 1000;

    info->time = (int)(now_ms - cur->start_time_ms);

    return 0;
}


int sys_mmap(uint64 start, uint64 len, int port, int flag, int fd)
{
    struct proc *p = curr_proc();

    if (len == 0)
        return 0;
    if (len > (1ULL << 30))
        return -1;
    if (port & ~0x7)
        return -1;
    if ((port & 0x7) == 0)
        return -1;

    if (start % PGSIZE != 0)
        return -1;

    uint64 first = start;
    uint64 last  = PGROUNDDOWN(start + len - 1);

    for (uint64 a = first; a <= last; a += PGSIZE) {
        if (walkaddr(p->pagetable, a) != 0)
            return -1;
    }

    int flags = PTE_U | PTE_V;
    if (port & 1) flags |= PTE_R;
    if (port & 2) flags |= PTE_W;
    if (port & 4) flags |= PTE_X;

    for (uint64 a = first; a <= last; a += PGSIZE) {
        char *pa = kalloc();
        if (!pa) return -1;
        memset(pa, 0, PGSIZE);
        if (mappages(p->pagetable, a, PGSIZE, (uint64)pa, flags) != 0)
            return -1;
    }

    return 0;
}

int sys_munmap(uint64 start, uint64 len, int port, int flag, int fd)
{
    struct proc *p = curr_proc();

    if (len == 0)
        return 0;

    if (start % PGSIZE != 0)
        return -1;

    uint64 first = PGROUNDDOWN(start);
    uint64 last  = PGROUNDDOWN(start + len - 1);

    for (uint64 a = first; a <= last; a += PGSIZE) {
        if (walkaddr(p->pagetable, a) == 0)
            return -1;
    }

    for (uint64 a = first; a <= last; a += PGSIZE) {
        uint64 pa = walkaddr(p->pagetable, a);
        kfree((void*)pa);
        uvmunmap(p->pagetable, a, 1, 0);
    }

    return 0;
}





extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/

	struct proc *cur = curr_proc();

	cur->syscall_count++;
	if (id < MAX_SYSCALL_NUM) {
		cur->syscall_times[id]++;  
	}

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_getpid:
    	ret = curr_proc()->pid;
    	break;
	case SYS_task_info:
		ret = sys_task_info((struct task_info *)args[0]);
    	break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;

	case SYS_munmap:
		ret = sys_munmap(args[0], args[1], args[2], args[3], args[4]);
		break;

	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
