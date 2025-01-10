#include <types.h>
#include <vfs.h>
#include <current.h>
#include <fsyscall.h>
#include <uio.h>
#include <vnode.h>
#include <kern/stat.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <copyinout.h>
#include <proc.h>
#include <thread.h>
#include <psyscall.h>
#include <addrspace.h>
#include <proc.h>
#include <syscall.h>


#define AVAILABLE 1
#define EXITED 2
#define ORPHAN 3
#define ZOMBIE 4
#define RUNNING 5

/*Driver code for fork() syscall*/
int 
sys_fork(struct trapframe *tf, size_t *retVal) {
    struct proc *childProc; 
    struct trapframe *child_tf;
    int ret;

	const char *childName = "childProc"; 
    childProc = proc_creator(childName); // proc_create will init and malloc child's file table
	ret = fork_proc(childProc);

    if(ret){ // fork_proc failed
        return ret; 
    }

	child_tf = set_tf(tf); 

	*retVal = childProc->pid;
	ret = thread_fork("childProc", childProc, exec_usermode, child_tf, 1);
	if (ret) {// Destroy proc if error in thread_fork
		proc_destroy(childProc);
		kfree(tf);
		return ret;
	}

	return 0;
}


void
exec_usermode(void *data1, unsigned long data2) {
    void *tf = (void *) curthread->t_stack + 16;
    memcpy(tf, (const void *) data1, sizeof(struct trapframe));
    kfree((struct trapframe *)data1);

    as_activate();
    mips_usermode(tf);
    if(data2) return ; 
}

int sys_getpid(size_t *retval){
    proc_getpid(retval); 
    return 0; 
}

/*Driver for waitpid*/
int 
sys_waitpid(pid_t pid, int *retVal, int options) {

    size_t exitcode = 0; 
    if(options != 0) {
        return EINVAL;
    }
    int canReap = 0; 
    if(retVal) canReap = 1; 
    if(retVal != NULL){
		int ret = copyout(&exitcode, (userptr_t) retVal, sizeof(int32_t));
		if (ret){
			canReap = 0; 
		}
	}

    int child_flag;
    child_flag = isChild(curproc, pid);
    if(child_flag == 0) {
        return ECHILD;
    }

    int err = proc_waitpid(pid, &exitcode, canReap); 
    if(err){
        return err; 
    }

    if(retVal != NULL){
		int ret = copyout(&exitcode, (userptr_t) retVal, sizeof(int32_t));
		if (ret){
			return ret;
		}
	}

    return 0;
}

/*Driver for __exit() syscall*/
void sys__exit(size_t exitcode){
    proc_exit(curproc, exitcode);
}

/*Get length of string, including the null terminator*/
int getLen(char *arg_string, int *len){
    int index = 0; 
    // char c = arg_string[index]; 

    char c; 
    int err = copyin((const_userptr_t)&arg_string[index], (void *) &c, (size_t) sizeof(char));
    if(err){
        return err; 
    }
    while(c && index < ARG_MAX){
        index++; 
        int err = copyin((const_userptr_t)&arg_string[index], (void *) &c, (size_t) sizeof(char));
        if(err){
            return err; 
        }
    }

    if(c){
        return E2BIG; 
    }

    *len = index + 1; 
    return 0; 
}


/*Get arguments from userspace to kernel space*/
int getargs(char **args, char **kern_args, int total_args){
    char *kaddr; 
    int len; 
    int err; 
    // kern_args[0] = &total_args; 
    for(int i = 0; i < total_args; i++){
        char *arg_ptr;
        err = copyin((const_userptr_t)&args[i], &arg_ptr, sizeof(char *));
        if (err) {
            return err;
        }

        err = getLen(arg_ptr, &len); 
        if(err){
            return err; 
        }
        kaddr = kmalloc(len); 
        err = copyinstr((const_userptr_t) arg_ptr, kaddr, len, NULL); 
        if(err){
            return err; 
        }
        kern_args[i] = kaddr; 
    }
    kern_args[total_args] = NULL;

    return 0; 
}

/*Method to get the total number of arguments, returns with error
if any copyin fails*/
int getCount(char **args, char *arg_addr, int *total_args){
    int index = 0; 
    int ret = copyin((const_userptr_t)&args[index], &arg_addr, (size_t) sizeof(char *));
    if(ret){ // If error during copyin, return the error
        return ret;
    }

    while(arg_addr && index < ARG_MAX){
        index++; 
        ret = copyin((const_userptr_t)&args[index], &arg_addr, (size_t) sizeof(char *));
        if(ret){ // If error during copyin, return the error
            return ret;
        }
    }

    if(arg_addr){
        return E2BIG; 
    }
    *total_args = index;
    
    return 0; 
}

/*Read program from userspace and put it in kernel*/
static int getProgName(const char *src_name, char *dest_name){
    int err; 
    int len; 
    err = getLen((char *) src_name, &len); 
    if(err){
        return err; 
    }
    
    size_t *stop_len = kmalloc(sizeof(int)); 
    err = copyinstr((const_userptr_t) src_name, dest_name, len, stop_len); 
    if(err){
        return err; 
    }

    return 0; 
}       

/*Free allocated memory*/
static int free_args(char **kern_args, int total_args){
    for(int i = 0; i < total_args; i++){
        kfree(kern_args[i]); 
    }

    kfree(kern_args); 
    return 0; 
}

/*Copy args from kernel to user stack*/
static int copyout_args(char **argumentStr, int total_args, vaddr_t *stackptr, userptr_t *args_out_addr) {
    vaddr_t arg_ptrs[total_args + 1]; // Array to store user-space pointers to arguments
    size_t arg_len;
    int err;

    for (int i = total_args - 1; i >= 0; i--) {
        if(!argumentStr[i]){
            return EFAULT; 
        }
        arg_len = strlen(argumentStr[i]) + 1; // Include null terminator

        // Adjust stack pointer for the argument string
        *stackptr -= ROUNDUP(arg_len, 8); // Align to 8 bytes for proper alignment
        err = copyout(argumentStr[i], (userptr_t)*stackptr, arg_len);
        if (err) {
            return err; // Return error if copyout fails
        }

        // Store the user-space address of the argument string
        arg_ptrs[i] = *stackptr;
    }

    // Last argument should be NULL for null termination
    arg_ptrs[total_args] = 0;

    // Copy the array of pointers to the stack
    *stackptr -= ROUNDUP((total_args + 1) * sizeof(vaddr_t), 8); // Align to 8 bytes
    *args_out_addr = (userptr_t)*stackptr; // Store the user-space address of the argv array
    err = copyout(arg_ptrs, *args_out_addr, (total_args + 1) * sizeof(vaddr_t));
    if (err) {
        return err; // Return error if copyout fails
    }

    *stackptr -= *stackptr % 8; // Ensure 8-byte alignment

    return 0;
}

/*Some parts of the code taken from runprogram.c*/
int sys_execv(const char *program, char **args){
    char *arguments = kmalloc(sizeof(char *));
    char **argumentStr; 
    char *progToCreate; 

    int total_args; 
    int err;

    int nameLen; 
    err = getLen((char *) program, &nameLen); 
    if(err){
        return err; 
    }
    progToCreate = kmalloc(nameLen); 

    err = getProgName(program, progToCreate);
    if(err){
        kfree(progToCreate); 
        return err; 
    }
    
    err = getCount(args, arguments, &total_args); 
    if(err){
        kfree(progToCreate);
        kfree(arguments);
        return err; 
    }

    argumentStr = kmalloc(total_args * sizeof(char *)); // memory allocated = total args * sizeof(arg);
    
    err = getargs(args, argumentStr, total_args); 
    if(err){
        kfree(progToCreate);
        kfree(arguments);
        free_args(argumentStr, total_args); 
        return err; 
    }

    /*Setting up addrspace*/
    struct addrspace *old_as; 
    struct addrspace *new_as;
    struct vnode *v;

    err = vfs_open(progToCreate, O_RDONLY, 0, &v); 
    if(err){
        kfree(progToCreate);
        kfree(arguments);
        free_args(argumentStr, total_args); 
        return err; 
    }

    old_as = proc_getas(); 
    new_as = as_create(); 

    if (new_as == NULL) {
		vfs_close(v);
        // NEED TO FREE OTHER THINGS TOO
        kfree(progToCreate);
        kfree(arguments);
        free_args(argumentStr, total_args); 
		return ENOMEM;
	}

    /*Switch to new addrspace*/

    /*Deactivate the old one*/
    as_deactivate(); 
    /*Activate the new one*/
    proc_setas(new_as); 
    as_activate(); 

    /*Load a new executable*/
    vaddr_t entrypoint; 
    err = load_elf(v, &entrypoint);
	if (err) {
		/* p_addrspace will go away when curproc is destroyed */
        /*Deactivate the new one*/
        as_deactivate(); 
        /*Activate the old one*/
        proc_setas(old_as); 
        as_activate(); 

		vfs_close(v);

        kfree(progToCreate);
        kfree(arguments);
        free_args(argumentStr, total_args); 
		return err;
	}

    /* Done with the file now. */
	vfs_close(v);

    vaddr_t stackptr;
    err = as_define_stack(new_as, &stackptr);
    if(err){
        as_deactivate(); 
        /*Activate the old one*/
        proc_setas(old_as); 
        as_activate(); 

        kfree(progToCreate);
        kfree(arguments);
        free_args(argumentStr, total_args); 
        return err; 
    }

    userptr_t args_out_addr; 

    err = copyout_args(argumentStr, total_args, &stackptr, &args_out_addr);
    if(err){
        as_deactivate(); 
        /*Activate the old one*/
        proc_setas(old_as); 
        as_activate(); 

        kfree(progToCreate);
        kfree(arguments);
        free_args(argumentStr, total_args); 
        return err; 
    }

    /*Clean up the old addr space*/
    kfree(progToCreate);  
    free_args(argumentStr, total_args); 

    /*Warp to user mode*/
    /* enter_new_process does not return. */
    enter_new_process(total_args, args_out_addr, NULL, stackptr, entrypoint);
	panic("enter_new_process returned\n");
	return EINVAL;
}

