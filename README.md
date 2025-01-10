# ed-operating-system

Project involves the development of core components of educational operating system kernel in C, focusing on system-level programming and low-level operating system design principles. This helped introduce core concepts of operating system development, including synchronization, process management, file systems, and virtual memory.

## Key Features Implemented

### 1. **System Calls**
   - Designed and implemented a set of system calls that interact with the kernel and allow processes to interact with the OS.
   - System calls covered include file operations (open, read, write, close), process management (fork, exec), and memory management.

### 2. **Process Management**
   - Developed process management functionalities to handle process creation, scheduling, and termination.
   - Implemented a basic round-robin scheduler to manage process execution.

### 3. **Synchronization Primitives**
   - Designed and implemented synchronization mechanisms like semaphores and locks to ensure safe concurrent access to shared resources.
   - Used these primitives to prevent race conditions and deadlocks in multi-threaded environments.

### 4. **Virtual Memory and TLB Management**
   - Implemented virtual memory management based on a page table.
   - Designed a TLB-based memory management system to optimize memory access and enhance performance.

## Technologies Used
- **Languages**: C
- **Tools**: GDB, Unix/Linux Environment
