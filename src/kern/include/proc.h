/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <spinlock.h>
#include <thread.h> /* required for struct threadarray */

#include <filetable.h>

struct addrspace;
struct vnode;

struct pid_table *pidTable; 

/*
 * Process structure.
 */
struct proc {
	char *p_name;			/* Name of this process */
	struct spinlock p_lock;		/* Lock for this structure */
	struct threadarray p_threads;	/* Threads in this process */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */
	struct vnode *p_cwd;		/* current working directory */

	/* add more material here as needed */
	struct fileTablePtr* ft; // File table pointer for the process

	struct array *children; // Children of a proc
	pid_t pid; // pid of proc
	int status; // status of proc
	int exitCode; // exitcode of proc
};

struct pid_table{
	int firstFreePid; // Indicates the pid that can be assigned
	struct proc *procs[PID_MAX + 1]; // Since cannot be used 
	struct lock* plock; // Lock for the table
	struct cv* pcv; // Conditional variable used in waitpid and exit
};

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *proc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *proc_setas(struct addrspace *);


/*Add an entry to the pid table*/
int addPidEntry(struct proc *child);

/*Update first free pointer*/
void findFirstFreePid(void); 

/*Adds a childProc to pid table, copies the parent's filetable 
and increases ref count to parent's cwd (taken from proc_create)*/
int fork_proc(struct proc* childProc);

/*Set the trapframe for a child given the parent's tf*/
struct trapframe *set_tf(struct trapframe *parent);

/*Initialize the global pid table*/
void pid_table_init(void); 

/*Helper that kills the parent, and assigns it children to kernel
proc*/
void proc_exit(struct proc* proc_to_exit, size_t exitcode);

/*Helper to create a proc, using proc_create*/
struct proc* proc_creator(const char *name); 

/*Helper to determine if process is child of parent process*/
int isChild(struct proc* parent, pid_t child_pid);

/*Helper to return status of process*/
int getStatus(struct proc* parent, pid_t child_pid);

/*Helper to clean the file table of a proc*/
void clean_child_file(struct proc *parent, pid_t child_pid);

/*Returns child of parent process*/
struct proc* getChild(struct proc *parent, pid_t child_pid);

/*Waits for proc status to change to Zombie,
status is checked atomically*/
int proc_waitpid(pid_t pid, size_t *exitcode, int canReap); 

/*Gets the pid of a proc atomically*/
void proc_getpid(size_t *retval);

#endif /* _PROC_H_ */
