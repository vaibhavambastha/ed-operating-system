#ifndef _PSYSCALL_H_
#define _PSYSCALL_H_

#include <mips/trapframe.h>

int sys_fork(struct trapframe *tf, size_t *retVal);
int sys_getpid(size_t *retval);
void sys__exit(size_t retval);

void exec_usermode(void *data1, unsigned long data2);

int sys_waitpid(pid_t pid, int *retVal, int options);
int getargs(char **args, char **kern_args, int total_args);
int getCount(char **args, char *arg_addr, int *total_args);
int sys_execv(const char *program, char **args);
int getLen(char *arg_string, int *len);

#endif /* _PSYSCALL_H_ */
