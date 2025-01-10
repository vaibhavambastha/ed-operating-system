#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>


#define DUMBVM_STACKPAGES    18

/*First valid page is 2, since 1 page is being used for meta data*/
#define FIRST_VALID 2

/*
 * Wrap ram_stealmem in a spinlock.
 */
// static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;


/*Single level page table to store mappings*/
static struct pageTable *pt; 

/*Initialize the page table and each page*/
void vm_bootstrap(void)
{
    paddr_t first_page;
    paddr_t last_page;
    
    // Obtain the size of the physical memory
    last_page = ram_getsize();
	first_page = ram_stealmem(0); // Get the address of first_page
    int total_pages = (last_page - first_page) / PAGE_SIZE; // Find total_pages
    
    // Allocate memory for the page table
    first_page = ram_stealmem(total_pages); // Steal total_pages amount of memory

    // Initialize the page table
    pt = (struct pageTable *)PADDR_TO_KVADDR(first_page);
    pt->total_pages = total_pages;
    pt->firstFreeIndex = FIRST_VALID;
    
    // Calculate the starting address for the pages array
    struct page *pages_base = (struct page *) PADDR_TO_KVADDR(first_page + PAGE_SIZE);
    
    for (int i = 1; i < total_pages; i++) {
        struct page *curr = &pages_base[i]; // Use array indexing to avoid incorrect address calculation
        curr->taken = 0; // Page is free at bootstrap
        curr->phys_addr = first_page + (i * PAGE_SIZE);
        curr->virtual_addr = PADDR_TO_KVADDR(curr->phys_addr);
		curr->master_index = i; 
        pt->pages[i] = curr; // Ensure correct indexing
    }
}

/*Method to check if npages are available in [start, start + npages)*/
static int checkRange(int start, int npages){
	for(int i = start; i < start + npages; i++){
		if(pt->pages[i]->taken){
			return 0; 
		}
	}
	return 1; 
}

/*Find next free page index starting from start + 1*/
static int find_next_free(int start){
	for(int i = start + 1; i < pt->total_pages; i++){
		if(!pt->pages[i]->taken){
			return i; 
		}
	}
	return -1; 
}

/*Mark the pages in [start, start + npages) as taken*/
static void make_unavailable(int start, int npages){
	int master = pt->pages[start]->master_index;
	for(int i = start; i < start + npages; i++){
		pt->pages[i]->taken = 1; 
		pt->pages[i]->master_index = master; 
	}
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	int npages_signed = npages; 
	int i = FIRST_VALID; 
	paddr_t toReturn; 
	while(i < pt->total_pages){
		if(i >= pt->total_pages){
			return ENOMEM; 
		}
		int pages_available = checkRange(i, npages_signed);  // Check if npages are available from current index
		if(pages_available){
			toReturn = pt->pages[i]->virtual_addr;
			make_unavailable(i, npages_signed);  
			return toReturn;  
		}
		else{
			i = find_next_free(i); 
			if(i == -1){
				return ENOMEM; // Out of memory is i < 0
			}
		}
	}
	return ENOMEM; 
}

/*Free the allocated block of pages starting from addr*/
void
free_kpages(vaddr_t addr)
{
	int master_index = 0; 
	for(int i = FIRST_VALID; i < pt->total_pages; i++){
		if(pt->pages[i]->virtual_addr == addr){
			master_index = i; 
			break; 
		}
	}
	for(int i = FIRST_VALID; i < pt->total_pages; i++){
		if(pt->pages[i]->master_index == master_index){
			pt->pages[i]->taken = 0; 
			pt->pages[i]->master_index = i; 
			break; 
		}
	}
}

/*We have not implemented the following 3 functions*/
void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	if(faulttype) return (faultaddress & 0);
	return 0; 
}