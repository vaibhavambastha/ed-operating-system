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

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <filetable.h>
#include <kern/errno.h>
#include <mips/trapframe.h>
#include <current.h>

#include <synch.h>

#define AVAILABLE 1
#define EXITED 2
#define ORPHAN 3
#define ZOMBIE 4
#define RUNNING 5

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;


	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}


	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	proc->children = array_create(); 
	proc->status = RUNNING;
	// if(name == "[kernel]"){


	// if(strcmp(name, "[kernel]") == 0){
	// 	proc->pid = 1;
	// 	pidTable->procs[1] = proc; 
	// 	pidTable->firstFreePid = 2; // Since create with [kernel] is only called once, pidTable init can be done here
	// 	// proc->ft = ft_init(); // Only make the ft for the kernel thread (MIGHT BE WRONG!!!!!!!!!!!)
	// }

	/*Link proc's ft to the ft returned by ft_init*/
	proc->ft = ft_init(); // MIGHT BE WRONG!!!!!!!!!!!!!!!!!!!

	threadarray_init(&proc->p_threads);
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */



		proc->status = EXITED; 
		// NEED TO CLEANUP FILETABLE

		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	lock_destroy(pidTable->plock); 
	cv_destroy(pidTable->pcv); 

	threadarray_cleanup(&proc->p_threads);
	spinlock_cleanup(&proc->p_lock);

	kfree(proc->p_name);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	// pid_table_init(); // Initialize the pid_table first, then create the kernel proc
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;
	int retval = 0; 

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/*Initialize stdin, stdout, and stderr*/
	retval = stdio_init(newproc->ft); 

	/*Return NULL if there was an error*/
	if(retval){
		return NULL; 
	}

	int err = addPidEntry(newproc); 
	if(err){
		return NULL; 
	}

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int result;
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	result = threadarray_add(&proc->p_threads, t, NULL);
	spinlock_release(&proc->p_lock);
	if (result) {
		return result;
	}
	spl = splhigh();
	t->t_proc = proc;
	splx(spl);
	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	unsigned i, num;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	/* ugh: find the thread in the array */
	num = threadarray_num(&proc->p_threads);
	for (i=0; i<num; i++) {
		if (threadarray_get(&proc->p_threads, i) == t) {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			spl = splhigh();
			t->t_proc = NULL;
			splx(spl);
			return;
		}
	}
	/* Did not find it. */
	spinlock_release(&proc->p_lock);
	panic("Thread (%p) has escaped from its process (%p)\n", t, proc);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

/*Assuming that parent is already in the pidTable*/
int addPidEntry(struct proc *child){
	// if(pidTable->plock->lk_holder->t_name) kprintf("Lock held by (add): %s.\n", pidTable->plock->lk_holder->t_name);
	lock_acquire(pidTable->plock); 
	// if(pidTable->plock->lk_holder->t_name) kprintf("Lock held by (add): %s.\n", pidTable->plock->lk_holder->t_name);
	if(pidTable->firstFreePid == -1){
		// kprintf("Lock held by (add): %s.\n", pidTable->plock->lk_holder->t_name);
		lock_release(pidTable->plock); 
		return ENPROC; 
	}
	// LOCK THIS
	pidTable->procs[pidTable->firstFreePid] = child; 
	child->pid = pidTable->firstFreePid; 
	child->status = RUNNING; 
	child->exitCode = (int) NULL; 
	// parent->children.add(child); 
	unsigned *index_ret = 0; 
	array_add(curproc->children, child, index_ret);
	
	findFirstFreePid(); 
	lock_release(pidTable->plock);
	// kprintf("Lock held by (add): %d.\n", pidTable->plock->taken); 
	return 0; 
}

void findFirstFreePid(){
	/*Starting from 1 because pid 0 is reserved for master process*/
	for(int i = 2; i < PID_MAX; i++){
		if(!pidTable->procs[i] || pidTable->procs[i]->status == AVAILABLE){
			pidTable->firstFreePid = i; 
			return ; 
		}
	}
	pidTable->firstFreePid = -1; 
}

int fork_proc(struct proc* childProc){
	int err;
	err = addPidEntry(childProc); // need to handle if error returned
	err = as_copy(curproc->p_addrspace, &childProc->p_addrspace);  // need to handle if error returned
	// childProc->ft = curproc->ft; // Chlid's ft points to the same ft as the parent's file table
	if(err){
		return err; 
	}

	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		childProc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	lock_acquire(pidTable->plock); 
	fork_ft(curproc->ft, childProc->ft); 
	lock_release(pidTable->plock); 
	if(err){
		return err; 
	}
	return 0; 
}

struct trapframe *set_tf(struct trapframe *parent){
	struct trapframe *child = kmalloc(sizeof(struct trapframe));
	memcpy(child, parent, sizeof(struct trapframe));
	child->tf_epc += 4; // To move past the fork call
	return child; 
}

void pid_table_init(){
	pidTable = kmalloc(sizeof(struct pid_table)); 
	pidTable->firstFreePid = 2; 
	pidTable->plock = lock_create("pid_lock"); 
	pidTable->pcv = cv_create("pid_cv"); 
	for(int i = 2; i <= PID_MAX; i++){
		pidTable->procs[i] = NULL;
	}

	pidTable->procs[1] = kproc; 
	kproc->pid = 1; 
	// pidTable->firstFreePid = 2; 
}

struct proc* proc_creator(const char *name){
	return proc_create(name); 
}

void proc_exit(struct proc* proc_to_exit, size_t exitcode){
	int length = array_num(proc_to_exit->children); 
	struct array *children = proc_to_exit->children; 
	struct proc* master = pidTable->procs[1]; 
	for(int i = 0; i < length; i++){
			if ( (((struct proc *) array_get(children, i))->status == RUNNING) || ((((struct proc *) array_get(children, i))->status == ZOMBIE))) {
				unsigned index_ret; 
    			int ret = array_add(master->children, ((struct proc *) array_get(children, i)), &index_ret);
				if(ret){
					return ; // Stop the process if there was an error while adding to the array
				}
			}
	}
	lock_acquire(pidTable->plock); 
	proc_to_exit->status = ZOMBIE; 
	proc_to_exit->exitCode = exitcode;
	cv_broadcast(pidTable->pcv, pidTable->plock); 
	lock_release(pidTable->plock);
	thread_exit();
}

// Determine if process is child of parent given child_pid
int isChild(struct proc* parent, pid_t child_pid) {
	lock_acquire(pidTable->plock); 
	int length = array_num(parent->children);
	for(int i = 0; i < length; i++) {
		if(((struct proc *) array_get(parent->children, i))->pid == child_pid) {
			lock_release(pidTable->plock); 
			return 1;
		}
	}
	lock_release(pidTable->plock); 
	return 0;
	
}

// Returns the status of the child process based on the parent process and child_pid
int getStatus(struct proc* parent, pid_t child_pid) {
	struct proc* child;
	child = getChild(parent, child_pid);
	return child->status;
}

// Returns child of parent process given child_pid
struct proc* getChild(struct proc *parent, pid_t child_pid) {
	int length = array_num(parent->children);
	for(int i = 0; i < length; i++) {
		if(((struct proc *) array_get(parent->children, i))->pid == child_pid) {  // Ensure process is child of parent
			return ((struct proc *) array_get(parent->children, i));
		}
	}
	return NULL; 
}

// Free file properties of child process 
void clean_child_file(struct proc *parent, pid_t child_pid) {
	struct proc* child;
	child = getChild(parent, child_pid);
	cleanup(child->ft);
}

// Waits for process to finish execution and then reaps it depending on the canReap flag
int proc_waitpid(pid_t pid, size_t *exitcode, int canReap){    

	lock_acquire(pidTable->plock); 
	int status = 0; 
	status = pidTable->procs[pid]->status; 

    while(status != ZOMBIE) { // Wait until process has finished execution dependent on condition variable
		cv_wait(pidTable->pcv, pidTable->plock); 
		status = pidTable->procs[pid]->status; 
    }
	lock_release(pidTable->plock); 

	if(canReap){
		lock_acquire(pidTable->plock); 
		pidTable->procs[pid]->status = AVAILABLE; 
		*exitcode = pidTable->procs[pid]->exitCode; 
		findFirstFreePid(); 
		lock_release(pidTable->plock); 
	}
	
	return 0; 
}

void proc_getpid(size_t *retval){
	lock_acquire(pidTable->plock); 
	*retval = curproc->pid; 
	lock_release(pidTable->plock); 
}