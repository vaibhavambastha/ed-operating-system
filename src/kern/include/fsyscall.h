#ifndef FSYSCALL_H_
#define FSYSCALL_H_

#include <vnode.h>
#include <limits.h>

int init(void); 
int sys_open(const char *filename, int flags, mode_t mode, size_t* retval);
int sys_close(int fd);
int sys_read(int fd, void *buf, size_t buflen, size_t *retVal);
int sys_write(int fd, const void *buf, size_t nbytes, size_t *retVal); 
int sys_lseek(int fd, off_t pos, int whence, size_t *retVal, size_t *retFlag);
int sys_dup2(int oldfd, int newfd, size_t *retval);
int sys_chdir(const char* pathname);
int sys__getcwd(char *buf, size_t buflen, size_t *retVal);
int check_fd(int fd);


#endif