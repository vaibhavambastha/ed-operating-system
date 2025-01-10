#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <vnode.h>
#include <limits.h>
#include <synch.h>

struct fte {
    int flag; // Indicates taken or not
    struct vnode *file; // Points to the file
    off_t offset; // Offset in the file
    int permissions; // Permissions allowed on the file
    struct lock *fte_lock; 
    
};

struct fileTablePtr{
    struct fte *ftp[OPEN_MAX]; // Short for File Table Pointer
    int firstFreeSpot; 
};


struct fileTablePtr* ft_init(void);

/*Add entry to filetable*/
int addEntry(struct fileTablePtr *ft, int permissions, off_t offset, struct vnode* vn, int *errFlag, int *fd); 

/*Initialize stdin, stdout, stderr.*/
int stdio_init(struct fileTablePtr *ft); 

/*Update the firstFreeSpot to earliest available open spot.*/
void findFirstFreeSpot(struct fileTablePtr *ft);

/*Add stdin, stdout, stderr entries to file table*/
void addIoEntry(struct fileTablePtr *ft, int permissions, struct vnode* vn, int fd);

/*Get entry at a particular fd.*/
struct fte *getEntry(struct fileTablePtr *ft, int fd); 

/*Reset a particular fd*/
void clean_fd(struct fileTablePtr *ft, int fd); 

/*Check validity of fd*/
int isValid(struct fileTablePtr *ft, int fd);

/*Duplicate a given fd*/
int addDupEntry(struct fileTablePtr *ft, int oldfd, int newfd, size_t *retval); 

/*Create file table entries for child proc after forking*/
struct fileTablePtr* fork_ft(struct fileTablePtr *parent, struct fileTablePtr *child_ft);

void cleanup(struct fileTablePtr *ft); 


#endif /* _FILETABLE_H_ */
