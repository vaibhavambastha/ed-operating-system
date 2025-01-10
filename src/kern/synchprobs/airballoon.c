/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16


static int ropes_left = NROPES;
// static int threads_left = 4; 
static int threads_left = 3 + N_LORD_FLOWERKILLER; 
/* Data structures for rope mappings */

/* Implement this! */
static volatile int mappings[NROPES]; // Each element at a given index represents a stake
static volatile int severed[NROPES]; // Shows whether a rope at a stake has been severed or not


/* Synchronization primitives */
struct lock *ropeLock[NROPES]; // One lock for each rope for concurrency
struct lock *rope_check_lock; // Lock to check and update the number of ropes
struct lock *print_lock; // Lock to print atomically (only used by FlowerKiller)

struct lock *threads_lock; // Lock to update the number of threads


/* Implement this! */

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;


	kprintf("Dandelion thread starting\n");
	thread_yield(); // Give other threads chance to init

	while(1){
		int hookIndex = random() % NROPES; // Get a random index to cut
		int ropeBeingCut = hookIndex; 
		
		lock_acquire(ropeLock[ropeBeingCut]); // Acquire the lock for the particular rope
		if(!severed[ropeBeingCut]){
			severed[ropeBeingCut] = 1; // If the rope is not severed, sever it
			// lock_acquire(print_lock); 
			kprintf("Dandelion severed rope %d\n", ropeBeingCut); 
			// lock_release(print_lock); 

			lock_acquire(rope_check_lock); // Acquire the rope_check_lock before updating the number of ropes
			ropes_left--; 
			lock_release(rope_check_lock); 
		}
		lock_release(ropeLock[ropeBeingCut]);

		lock_acquire(rope_check_lock); // Acquire the rope_check_lock again to read the value of ropes_left
		if(ropes_left <= 0){ // Break out of the while loop if no more ropes are left
			lock_release(rope_check_lock); 
			break; 
		} 
		lock_release(rope_check_lock); 

		thread_yield(); // Give other threads a chance to run after finishing a round of the loop 
		
	}

	/* Implement this function */
	// lock_acquire(print_lock); 
	
	// lock_release(print_lock); 

	lock_acquire(threads_lock); // Acquire the threads_lock before updating the threads_left
	kprintf("Dandelion thread done\n"); 
	threads_left--; // Decrement the number of threads_left 
	lock_release(threads_lock); 

	thread_exit(); // Exit the thread
}

static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");
	thread_yield(); // Give other threads chance to init

	while(1){
		int index = random() % NROPES; // Generate random stake Index to sever
		int ropeBeingCut = mappings[index]; // The rope can be found by accessing the particular index in the mappings array
		
		lock_acquire(ropeLock[ropeBeingCut]);
		if(!severed[ropeBeingCut] && ropeBeingCut == mappings[index]){ // If the given rope is not severed, sever it, second check ensures that flowerKiller didn't change the rope after marigold read it
			severed[ropeBeingCut] = 1; 
			// lock_acquire(print_lock); 
			kprintf("Marigold severed rope %d from stake %d\n", ropeBeingCut, index); 
			// lock_release(print_lock); 

			lock_acquire(rope_check_lock); // Acquire the rope_check_lock before updating the number of ropes
			ropes_left--; 
			lock_release(rope_check_lock); 
		}
		lock_release(ropeLock[ropeBeingCut]);

		// thread_yield(); 

		lock_acquire(rope_check_lock); // Acquire the rope_check_lock again to read the value of ropes_left
		if(ropes_left <= 0){ // Break out of the while loop if no more ropes are left
			lock_release(rope_check_lock); 
			break; 
		} 
		lock_release(rope_check_lock); 

		thread_yield(); // Give other threads a chance to run after finishing a round of the loop
	}


	lock_acquire(threads_lock); // Acquire the threads_lock before updating the threads_left
	kprintf("Marigold thread done\n");
	threads_left--; // Decrement the number of threads_left 
	lock_release(threads_lock); 

	thread_exit(); // Exit the thread

	/* Implement this function */
}



static
void
flowerkiller(void *p, unsigned long arg)
{
    (void)p;
    (void)arg;

    kprintf("Lord FlowerKiller thread starting\n");
	thread_yield(); // Give other threads chance to init

    while(1) {
        int stake1Ind = random() % NROPES; // Generate the first random stake indices
        int stake2Ind = random() % NROPES; // Generate the second random stake indices

        // Ensure two different stakes are selected
        if (stake1Ind == stake2Ind) { 
            continue; // Skip if both are the same, retry the loop
        }

        int rope1 = mappings[stake1Ind]; // Find the ropes at the particular stake indices
        int rope2 = mappings[stake2Ind]; // Find the ropes at the particular stake indices

        // Determine consistent order for locks
        int firstLock = rope1 < rope2 ? rope1 : rope2;  // Done so that if 2 locks want the same stakes, they don't get stuck in a deadlock
        int secondLock = rope1 < rope2 ? rope2 : rope1; // Done so that if 2 locks want the same stakes, they don't get stuck in a deadlock

        // Try to acquire the first lock
        lock_acquire(ropeLock[firstLock]); // Acquire the first lock

        // Try to acquire the second lock
        if (!lock_do_i_hold(ropeLock[secondLock]) && lock_do_i_hold(ropeLock[firstLock])) { // Ensure that the current thread does not have the second lock but does have the first
            lock_acquire(ropeLock[secondLock]);

            // Perform the swap if neither rope is severed
            if (!severed[rope1] && !severed[rope2] && rope1 == mappings[stake1Ind] && rope2 == mappings[stake2Ind]) {// Only swap if both ropes have NOT been severed and the mappings did not change after acquiring the lock
                int temp = mappings[stake1Ind]; 
                mappings[stake1Ind] = mappings[stake2Ind]; 
                mappings[stake2Ind] = temp; // Swap the ropes

				lock_acquire(print_lock); // Acquire the print lock so no other FlowerKiller prints swapping in the middle
                kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope1, stake1Ind, stake2Ind); 
                kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", rope2, stake2Ind, stake1Ind); 
				lock_release(print_lock);
			}
            // Release the second lock
            lock_release(ropeLock[secondLock]);
        }

        // Release the first lock
        lock_release(ropeLock[firstLock]);

        // Check if all ropes are severed
        lock_acquire(rope_check_lock); 
        if (ropes_left <= 0) { // Same logic as Marigold and Dandelion
            lock_release(rope_check_lock); 
            break; 
        }
        lock_release(rope_check_lock); 

        thread_yield();
    }


    lock_acquire(threads_lock); // Same logic as Marigold and Dandelion
	kprintf("Lord FlowerKiller thread done\n"); 
    threads_left--; 
    lock_release(threads_lock); 


    thread_exit();
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");
	thread_yield(); // Give other threads chance to init

	while(1){
		lock_acquire(rope_check_lock); 
		if(ropes_left <= 0){ // If no more ropes left, escape is successful
			kprintf("Balloon freed and Prince Dandelion escapes!\n"); 
			lock_release(rope_check_lock);
			break; 
		}
		lock_release(rope_check_lock); 
		thread_yield(); 
	}

	/* Implement this function */


	lock_acquire(threads_lock); 
	kprintf("Balloon thread done\n"); 
	threads_left--; 
	lock_release(threads_lock); 

	thread_exit();
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

	for(int j = 0; j < NROPES; j++){
		mappings[j] = j; // Create the mappings, initially, its a 1:1 relation between ropes and stakes
		severed[j] = 0; // All ropes are not severed initially
		ropeLock[j] = lock_create(""); // Initialize locks for each rope
	}

	rope_check_lock = lock_create("RCL"); // Initialize the rope_check_lock ("RCL")
	print_lock = lock_create("PL"); // Initialize the print_lock ("PL")
	threads_lock = lock_create("TL"); // Initialize the threads_lock ("TL")

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:



	while(1){
		lock_acquire(rope_check_lock); 
		if(ropes_left <= 0){ // If no more ropes left, break out of the while loop
			lock_release(rope_check_lock); 
			break; 
		}
		lock_release(rope_check_lock); 
		thread_yield(); // If not, yield to the other threads
	}

	while(1){
		lock_acquire(threads_lock);

		if(threads_left <= 0){ // If no more threads left, break out of the while loop
			lock_release(threads_lock); 
			break; 
		}
		lock_release(threads_lock); 
		thread_yield(); // If not, yield to the other threads
	}

	
	kprintf("Main thread done\n"); 

	ropes_left = NROPES; // Reset the number of ropes so tests can be run again

	for(int i = 0; i < NROPES; i++){ // Destroy all rope locks
		lock_destroy(ropeLock[i]); 
	}
	
	lock_destroy(rope_check_lock); // Destroy the rope_check_lock
	lock_destroy(print_lock); // Destroy the print_lock
	lock_destroy(threads_lock); // Destroy the print_lock

	return 0;
}


// Comment