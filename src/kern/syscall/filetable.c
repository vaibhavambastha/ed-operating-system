#include <types.h>
#include <lib.h>
#include <vfs.h>
#include <current.h>
#include <fsyscall.h>
#include <uio.h>
#include <vnode.h>
#include <kern/stat.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <copyinout.h>
#include <limits.h>
#include <proc.h>
#include <current.h>

#include <filetable.h>
#include <synch.h>


// static int firstFreeSpot = 0; // This variable always indicates the first free spot, -1 indicates no free spots available
// static struct fileTablePtr *ft; // The file table

struct fileTablePtr* ft_init(void){
    // struct fileTablePtr *ft; 
    struct fileTablePtr *ft = kmalloc(sizeof(struct fileTablePtr));

    ft->firstFreeSpot = 0; 
    // ft = kmalloc(sizeof(struct fileTablePtr)); 
    for (int i = 0; i < OPEN_MAX; i++){
        ft->ftp[i] = kmalloc(sizeof(struct fte));  
        ft->ftp[i]->flag = 0; // Initally all spots are open
        ft->ftp[i]->file = 0; // Initially all vnode pointers are null
        ft->ftp[i]->offset = 0; // Initially all offsets are 0
        ft->ftp[i]->permissions = 0; // No permissions initially
        ft->ftp[i]->fte_lock = lock_create("fte lock"); 
    }

    return ft; 
}

int addEntry(struct fileTablePtr *ft, int permissions, off_t offset, struct vnode* vn, int *errFlag, int *fd){ // UPDATE firstFreeSpot in this
    if(ft->firstFreeSpot == -1){
        *errFlag = 1; 
        return ENOMEM; 
    }
    lock_acquire(ft->ftp[ft->firstFreeSpot]->fte_lock);
    ft->ftp[ft->firstFreeSpot]->flag = 1; // Change flag to 1 to indicate position occupied
    ft->ftp[ft->firstFreeSpot]->file = vn; // Assign the file in argument to that spot's file field
    ft->ftp[ft->firstFreeSpot]->offset = offset; // Change the offset of that spot
    ft->ftp[ft->firstFreeSpot]->permissions = permissions; // Change the permissions
    *fd = ft->firstFreeSpot; // fd points to the spot obtained
    lock_release(ft->ftp[ft->firstFreeSpot]->fte_lock);
    findFirstFreeSpot(ft); // Update firstFreeSpot
    return 0; 
} 

/*This method is almost identical to addEntry, only difference is the fd is given (0, 1, 2) and does not need to be returned/updated.*/
void addIoEntry(struct fileTablePtr *ft, int permissions, struct vnode* vn, int fd){
    lock_acquire(ft->ftp[fd]->fte_lock);
    ft->ftp[fd]->flag = 1; 
    ft->ftp[fd]->file = vn; 
    ft->ftp[fd]->offset = 0; 
    ft->ftp[fd]->permissions = permissions;
    lock_release(ft->ftp[fd]->fte_lock);  
    findFirstFreeSpot(ft);
}

void findFirstFreeSpot(struct fileTablePtr *ft){
    for(int i = 3; i < OPEN_MAX; i++){
        lock_acquire(ft->ftp[i]->fte_lock);
        if(ft->ftp[i]->flag == 0){ // Find the earliest entry which is unoccupied. 
            ft->firstFreeSpot = i;
            lock_release(ft->ftp[i]->fte_lock); 
            return; 
        }
        lock_release(ft->ftp[i]->fte_lock);
    }
    ft->firstFreeSpot = -1; // If no free spots, set it to -1 to indicate that
}

int stdio_init(struct fileTablePtr *ft){
    struct vnode *stdin_vnode;   // Pointer to stdin vnode
    struct vnode *stdout_vnode;  // Pointer to stdout vnode
    struct vnode *stderr_vnode;  // Pointer to stderr vnode

    // Initialize the first 3 indices
    const char *std = "con:"; // Reference to stdin
    int result = 0; 

    result = vfs_open(kstrdup(std), O_RDONLY, 0, &stdin_vnode); // Point to stdin vnode
    if(result){
        return result; 
    }
    addIoEntry(ft, 1, stdin_vnode, 0); 

    result = vfs_open(kstrdup(std), O_WRONLY, 0, &stdout_vnode); // Point to stdout vnode
    if(result){
        return result; 
    } 
    addIoEntry(ft, 2, stdout_vnode, 1);

    result = vfs_open(kstrdup(std), O_WRONLY, 0, &stderr_vnode); // Point to err vnode
    if(result){
        return result; 
    }
    addIoEntry(ft, 2, stderr_vnode, 2); 

    return 0; 
}

struct fte *getEntry(struct fileTablePtr *ft, int fd){ 
    return ft->ftp[fd]; // Get the entry at the specified fd
}

void clean_fd(struct fileTablePtr *ft, int fd){
    lock_acquire(ft->ftp[fd]->fte_lock);
    ft->ftp[fd]->flag = 0; // Set flag to 0 to indicate spot is free
    ft->ftp[fd]->file = 0; // Change the file pointer to NULL
    ft->ftp[fd]->offset = 0; // Reset offset
    ft->ftp[fd]->permissions = 0; // Reset permissions
    lock_release(ft->ftp[fd]->fte_lock);
    findFirstFreeSpot(ft); // Update firstFreeSpot since fd might be before firstFreeSpot
}

/*This method is used to check whether the given fd is valid and has read or write permissions*/
int isValid(struct fileTablePtr *ft, int fd){
    lock_acquire(ft->ftp[fd]->fte_lock);
    if((ft->ftp[fd]->flag == 1) && ((ft->ftp[fd]->permissions == 1) || ft->ftp[fd]->permissions == 3)) {
        lock_release(ft->ftp[fd]->fte_lock);
        return 1;    
    }

    lock_release(ft->ftp[fd]->fte_lock);
    return 0;
}

/*This method is used to duplicate an existing entry*/
int addDupEntry(struct fileTablePtr *ft, int oldfd, int newfd, size_t *retval){
    lock_acquire(ft->ftp[oldfd]->fte_lock);
    if(oldfd == newfd){ // If old fd is same as new, retval points to newfd and return 0 to indicate success
        *retval = newfd; 
        lock_release(ft->ftp[oldfd]->fte_lock);
        return 0; 
    }

    if(ft->ftp[oldfd]->flag == 0){ // oldfd is invalid if its unoccupied
        lock_release(ft->ftp[oldfd]->fte_lock);
        return EBADF;
    }
    if(ft->firstFreeSpot == -1){ // Global limit on open files reached
        lock_release(ft->ftp[oldfd]->fte_lock);
        return ENFILE;
    }
    lock_release(ft->ftp[oldfd]->fte_lock);

    /*Lines 134-136 are just used to point the fields of newfd to oldfd*/
    lock_acquire(ft->ftp[newfd]->fte_lock);
    ft->ftp[newfd]->permissions = ft->ftp[oldfd]->permissions;
    ft->ftp[newfd]->file = ft->ftp[oldfd]->file;
    ft->ftp[newfd]->offset = ft->ftp[oldfd]->offset;
    ft->ftp[newfd]->flag = 1; // newfd is now occupied

    *retval = newfd; // Update retval to point to newfd
    lock_release(ft->ftp[newfd]->fte_lock);

    findFirstFreeSpot(ft); // Update firstFreeSpot

    return 0; // Return 0 to indicate success
}

/*Create file table entries for child proc after forking*/
struct fileTablePtr* fork_ft(struct fileTablePtr *parent, struct fileTablePtr *child_ft){
    for(int i = 0; i < OPEN_MAX; i++){
        lock_acquire(parent->ftp[i]->fte_lock);
        lock_acquire(child_ft->ftp[i]->fte_lock);
        if(parent->ftp[i]->flag == 1){
            child_ft->ftp[i] = parent->ftp[i]; 
        }  
        lock_release(parent->ftp[i]->fte_lock);
        lock_release(child_ft->ftp[i]->fte_lock);        
    }
    findFirstFreeSpot(child_ft); 
    return child_ft; 
}

void cleanup(struct fileTablePtr *ft){
    for(int i = 0; i < OPEN_MAX; i++){
        lock_destroy(ft->ftp[i]->fte_lock);
        kfree(ft->ftp[i]);
        ft->ftp[i] = NULL; 
    }
    kfree(ft); 
}