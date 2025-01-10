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
#include <limits.h>
#include <proc.h>
#include <filetable.h>
#include <kern/seek.h>

int sys_open(const char *filename, int flags, mode_t mode, size_t* retval){
    char *dest = kmalloc(PATH_MAX); // Allocate memory for the destination string
    if(dest == NULL){
        return ENOMEM; // If kmalloc fails, no memory left
    }

    /*Check validity of flags before trying to open the file*/
    int how = flags & O_ACCMODE; 
        /*Code taken from fcntl.h*/
        int permissions = 0; 
	    switch (how) {
	        case O_RDONLY: permissions = 1;
	    	break;
	        case O_WRONLY: permissions = 2; 
            break; 
	        case O_RDWR: permissions = 3; 
	    	break;
	        default: 
	    	return EINVAL;
	    }

    if (flags & O_EXCL) {
        if (!(flags & O_CREAT)) {
            return EINVAL; // O_EXCL is only valid with O_CREAT
        }
    }

    size_t len; // Variable to hold the length of the copied string

    int error = copyinstr((const_userptr_t) filename, dest, PATH_MAX, &len); // Check validity of filename

    /*Return error if filename is problematic*/
    if(error){
        kfree(dest); 
        return error; 
    }

    /*Initialize errFlag and fd to 0. These will be updated by addEntry*/
    int errFlag = 0; 
    int fd = 0; 

    /*ret stores the actualy file address*/
    struct vnode *ret; 
    int opened = vfs_open(dest, flags, mode, &ret); 

    if(opened != 0){
        return opened; // If opened is not 0, that means there was an error while in vfs_open so return the error code
    }
    
    /*Add the entry to the filetable*/
    int result = addEntry(curproc->ft ,permissions, 0, ret, &errFlag, &fd);

    /*If the errFlag was raised by addEntry, there is an error. Return the error*/
    if(errFlag){
        return result; 
    }

    /*Update retval to point to the obtained fd*/
    *retval = fd; 

    /*Return 0 to indicate success*/
    return 0; 
}


int sys_close(int fd) {
    struct fte* entry = getEntry(curproc->ft, fd);
    lock_acquire(entry->fte_lock);
    if(check_fd(fd) == 0 || entry->flag == 0) {  // Verify fd is valid and not already closed
        lock_release(entry->fte_lock);
        return EBADF;
    } 
    lock_release(entry->fte_lock);
    clean_fd(curproc->ft, fd);   // Clean fields of fd
    
    return 0;
}

int sys_write(int fd, const void *buf, size_t nbytes, size_t *retVal) {
    if(check_fd(fd) == 0 || getEntry(curproc->ft, fd)->file == NULL) {   // Verify fd is valid and file exists
        return EBADF;
    }

    struct fte *entry = getEntry(curproc->ft, fd);
    lock_acquire(entry->fte_lock);

    if(entry->permissions == 1) {  // Verify that fd was opened with write permissions
        lock_release(entry->fte_lock);
        return EBADF;
    }

    if(buf == NULL) {   // Buf is invalid
        lock_release(entry->fte_lock);
        return EFAULT;
    }

    int result;                // Store result of write operations
    struct iovec iov;        // I/O vector to to describe memory buffer
    struct uio ui;          // Manage write operations

    iov.iov_ubase = (userptr_t)buf;   // Base address of buffer
    iov.iov_len = nbytes;           // Set length of buffer

    ui.uio_iov = &iov;              
    ui.uio_iovcnt = 1;              // Num of I/O vectors
    ui.uio_offset = entry->offset;  // Offset in file
    ui.uio_resid = nbytes;          // Remaining bytes to write
    ui.uio_segflg = UIO_USERSPACE;  // Buffer is in user space
    ui.uio_rw = UIO_WRITE;         // Write operation
    ui.uio_space = proc_getas();   // Address space of current process

    result = VOP_WRITE(entry->file, &ui);    // Write operation using VOP
    if(result) {
        lock_release(entry->fte_lock);
        return result;
    }

    off_t bytes_written = (off_t)(nbytes - ui.uio_resid);   // Number of bytes written
    entry->offset += bytes_written;

    *retVal = (size_t) bytes_written;   // Return number of bytes written
    lock_release(entry->fte_lock);
    return 0;
}


int sys_read(int fd, void *buf, size_t buflen, size_t *retVal){ // RETURN TYPE SHOULD BE ssize_t
    

    if(check_fd(fd) == 0 || !isValid(curproc->ft, fd)){
        return EBADF;
    }

    if(!buf){
        return EFAULT; 
    }

    struct iovec iov; 
    struct uio ui; 
    struct fte *entry = getEntry(curproc->ft, fd);

    lock_acquire(entry->fte_lock); 

    iov.iov_ubase = (userptr_t) buf; // User buffer
    iov.iov_len = buflen;        // Length of the buffer
    ui.uio_iov = &iov;           // Iovec structure
    ui.uio_iovcnt = 1;           // One iovec
    ui.uio_resid = buflen;       // Remaining amount to read
    ui.uio_offset = entry->offset; // Current file offset
    ui.uio_segflg = UIO_USERSPACE; // User space memory
    ui.uio_rw = UIO_READ;         // Reading operation
    ui.uio_space = proc_getas();  // Address space of the current process

    int result = VOP_READ(entry->file, &ui);
    if(result){
        lock_release(entry->fte_lock);
        return result; 
    }

    entry->offset = entry->offset + (off_t)((buflen - ui.uio_resid)); // In case all of buflen wasn't read, need to account for the residual 

    *retVal = (buflen - ui.uio_resid);
    lock_release(entry->fte_lock); 
    return 0; 
}

int sys_lseek(int fd, off_t pos, int whence, size_t *retValUpper, size_t *retValLower) {
    if(whence < 0 || whence > 2) {  // Check if whence is a valid value
        return EINVAL;
    }

    if(check_fd(fd) == 0 || getEntry(curproc->ft, fd)->file == NULL) {   //Check if valid fd
        return EBADF;
    }

    
    struct fte *entry = getEntry(curproc->ft, fd);

    lock_acquire(entry->fte_lock);

    if(!VOP_ISSEEKABLE(entry->file)) {  // Check if fd refers to object not supporting seeking
        lock_release(entry->fte_lock); 
        return ESPIPE;
    }

    struct stat *st;
    off_t seek_pos;                         // Initialize seek position mark of file handle
    off_t eof;                           // Initialize end of file mark
    st = kmalloc(sizeof(struct stat));   // Allocate a struct stat object to memory
    VOP_STAT(entry->file, st);

    seek_pos = entry->offset;    // Assign current seek position
    eof = st->st_size;          // Assign end of file position




    switch(whence) {   // Seek_pos value based on whence
        case SEEK_SET:
        seek_pos = pos;
        break;

        case SEEK_CUR:
        seek_pos += pos;
        break;

        case SEEK_END:
        seek_pos = eof + pos;
        break;
    }

    if(seek_pos < 0) {   // If new seek position is invalid
        kfree(st);
        lock_release(entry->fte_lock);
        return EINVAL;
    }

    // Since seek_pos is 64 bits and registers can hold max 32 bits,
    // break seek_pos into 2 upper and lower 32 bit values 
    *retValUpper = seek_pos >> 32; 
    *retValLower = seek_pos & 0xFFFFFFFF; 

    entry->offset = seek_pos;   // Assign new offset value to file

    kfree(st);
    lock_release(entry->fte_lock);
    return 0;    
}

int sys_dup2(int oldfd, int newfd, size_t *retval){
    /*If either of oldfd or newfd are invalid, return bad address error*/
    if(!check_fd(oldfd) || !check_fd(newfd)){
        return EBADF; 
    }
    
    /*If oldfd is equal to newfd, point retval to newfd and return 0 to indicate success*/
    if(oldfd == newfd){
        *retval = newfd; 
        return 0; 
    }
    
    /*Get current entry at newfd*/
    struct fte *currEntry = getEntry(curproc->ft, newfd); 
    
    lock_acquire(currEntry->fte_lock);

    /*If current entry occupied, close it*/
    if(currEntry->flag == 1){
        lock_release(currEntry->fte_lock);
        sys_close(newfd); 
    }
    
    lock_release(currEntry->fte_lock);
    /*Duplicate the entry. If addDupEntry returns non zero value, that indicates an error, so return it*/
    int result = addDupEntry(curproc->ft, oldfd, newfd, retval); 
    if(result){
        return result; 
    }

    /*Return 0 to indicate success*/
    return 0; 
}


int sys_chdir(const char* pathname) {
    char *dest = kmalloc(PATH_MAX); // Allocate memory for the destination string
    
    if(dest == NULL){  // If memory allocation fails
        return ENOMEM; 
    }
    
    if(pathname == NULL) { 
        return EFAULT; 
    }
    
    size_t len; 

    int error = copyinstr((const_userptr_t) pathname, dest, PATH_MAX, &len); // Copy user given pathname into kernel space

    if(error) {      // If copy returned an error
        kfree(dest);
        return error;
    }

    int result = vfs_chdir((char *) pathname); // Change current working directory to given pathname

    kfree(dest); 

    if(result) {
        return result;
    } 

    return 0;

}

int sys__getcwd(char *buf, size_t buflen, size_t *retVal) {
    if(buf == NULL) {
        return EFAULT;
    }

    struct iovec iov; // I/O vector struct to describe memory buffer
    struct uio ui;    // User I/O struct to manage operation  
 
    iov.iov_ubase = (userptr_t) buf;   // Base address of buffer
    iov.iov_len = buflen;              // Set length of buffer

    ui.uio_iov = &iov;              
    ui.uio_iovcnt = 1;               // Num of I/O vectors
    ui.uio_offset = 0;               // No offset for current directory
    ui.uio_resid = buflen;           // Remaining bytes to read into the buffer
    ui.uio_segflg = UIO_USERSPACE;   // Buffer is in user space
    ui.uio_rw = UIO_READ;            // Operation is read
    ui.uio_space = proc_getas();     // Get address space of current process

    int result = vfs_getcwd(&ui);    // Get current working directory

    if(result) {
        return result;
    }

    *retVal = buflen - ui.uio_resid;   // Return number of bytes written to buffer

    return 0;
}

// Verifies that fd is within appropriate bounds
int check_fd(int fd) {  
    if(fd >= 0 && fd < OPEN_MAX) {
        return 1;
    }
    return 0;
}