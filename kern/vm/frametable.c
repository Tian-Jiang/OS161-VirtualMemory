#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>

/* 
 * Place your frametable data-structures here 
 * You probably also want to write a frametable initialisation
 * function and call it from vm_bootstrap
 */

static struct spinlock frametable_lock = SPINLOCK_INITIALIZER;

/* Note that this function returns a VIRTUAL address, not a physical 
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */

/*
 * Make variables static to prevent it from other file's accessing
 */
static struct frame_table_entry *frame_table;
static paddr_t frametop, freeframe;

/*
 * initialise frame table
 */
void
frametable_bootstrap(void)
{
	struct frame_table_entry *p;
	paddr_t firsta, lasta, paddr;
	unsigned long framenum, entry_num, frame_table_size, i;
	
	// get the useable range of physical memory
	ram_getsize(&firsta, &lasta);
	KASSERT((firsta & PAGE_FRAME) == firsta);
	KASSERT((lasta & PAGE_FRAME) == lasta);
	
	framenum = (lasta - firsta) / PAGE_SIZE;
	
	// calculate the size of the whole framemap
	frame_table_size = framenum * sizeof(struct frame_table_entry);
	frame_table_size = ROUNDUP(frame_table_size, PAGE_SIZE);
	entry_num = frame_table_size / PAGE_SIZE;
	KASSERT((frame_table_size & PAGE_FRAME) == frame_table_size);
	
	frametop = firsta;
	freeframe = firsta + frame_table_size;
	
	if (freeframe >= lasta) {
		// This is impossible for most of the time
		panic("vm: framemap consume physical memory?\n");
	}
	
	// keep the frame state in the top of the useable range of physical memory
	// the free frame page address started from the end of the frame map
	frame_table = (struct frame_table_entry *) PADDR_TO_KVADDR(firsta);
	
	// Initialise the frame list, each entry corrsponding to a frame,
	// and each entry stores the address of the next free frame.
	// If the next frame address of this entry equals zero, means this current frame is allocated
	p = frame_table;
	for (i = 0; i < framenum-1; i++) {
		if (i < entry_num) {
			p->next_freeframe = 0;
			p += 1;
			continue;
		}
		paddr = frametop + (i+1) * PAGE_SIZE;
		p->next_freeframe = paddr;
		p += 1;
	}
}

/*
 * Allocate n pages. 
 * Before frame table initialisation, using ram_stealmem
 */
static
paddr_t
getppages(int npages)
{
	paddr_t paddr;
	struct frame_table_entry *p;
	int i;
	
	spinlock_acquire(&frametable_lock);
	if (frame_table == 0)
		paddr = ram_stealmem(npages);
	else
	{
		if (npages > 1){
			spinlock_release(&frametable_lock);
			return 0;
		}
		
		// Freeframe equals zero means all the frames have been allocated
		// and there is no frame to use.
		if (freeframe == 0){
			spinlock_release(&frametable_lock);
			return 0;
		}
		
		// Get the current free frame's entry id 
		// and retrieve the next free frame 
		paddr = freeframe;
		i = (freeframe - frametop) / PAGE_SIZE;
		p = frame_table + i;
		
		freeframe = p->next_freeframe;
		p->next_freeframe = 0;
	}
	spinlock_release(&frametable_lock);
	
	return paddr;
}

/*
 * Allocation function for public accessing
 * Returning virtual address of frame
 */
vaddr_t
alloc_kpages(int npages)
{
	paddr_t paddr = getppages(npages);
	
	if(paddr == 0)
		return 0;
	
	return PADDR_TO_KVADDR(paddr);
}

/*
 * Free page
 * Stores the address of the current freeframe into the entry of the frame to be freed
 * and update the address of the freeframe.
 */
static
void
freeppages(paddr_t paddr)
{
	struct frame_table_entry *p;
	int i;
	spinlock_acquire(&frametable_lock);
	i = (paddr - frametop) / PAGE_SIZE;
	p = frame_table + i;
	p->next_freeframe = freeframe;
	freeframe = paddr;
	spinlock_release(&frametable_lock);
}

/*
 * Free page function for public accessing
 */
void
free_kpages(vaddr_t addr)
{
	KASSERT(addr >= MIPS_KSEG0);
	
	paddr_t paddr = KVADDR_TO_PADDR(addr);
	if (paddr <= frametop) {
		// memory leakage
	}
	else {
		freeppages(paddr);
	}
}

