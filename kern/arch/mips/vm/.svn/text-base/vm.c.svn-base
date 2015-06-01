#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <spl.h>
#include <spinlock.h>
#include <elf.h>

/*
 * Initialise the frame table
 */
void
vm_bootstrap(void)
{
	frametable_bootstrap();
}

/*
 * When TLB miss happening, a page fault will be trigged.
 * The way to handle it is as follow:
 * 1. check what page fault it is, if it is READONLY fault, 
 *    then do nothing just pop up an exception and kill the process
 * 2. if it is a read fault or write fault
 *    1. first check whether this virtual address is within any of the regions
 *       or stack of the current addrspace. if it is not, pop up a exception and
 *       kill the process, if it is there, goes on. 
 *    2. then try to find the mapping in the page table, 
 *       if a page table entry exists for this virtual address insert it into TLB 
 *    3. if this virtual address is not mapped yet, mapping this address,
 *	 update the pagetable, then insert it into TLB
 */
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t *vaddr1, *vaddr2, vaddr, vbase, vtop, faultadd = 0;
	paddr_t paddr;
	struct addrspace *as;
	struct as_region *s;
	uint32_t ehi, elo;
	int i, index1, index2, spl;
	unsigned int permis = 0;
	
	switch (faulttype) {
		case VM_FAULT_READONLY:
			return EFAULT;
		case VM_FAULT_READ:
		case VM_FAULT_WRITE:
			break;
		default:
			return EINVAL;
	}
	
	as = curthread -> t_addrspace;
	if (as == NULL) {
		return EFAULT;
	}
	
	// Align faultaddress
	faultaddress &= PAGE_FRAME;
	
	// Go through the link list of regions 
	// Check the validation of the faultaddress
	KASSERT(as->as_regions_start != 0);
	s = as->as_regions_start;
	while (s != 0) {
		KASSERT(s->as_vbase != 0);
		KASSERT(s->as_npages != 0);
		KASSERT((s->as_vbase & PAGE_FRAME) == s->as_vbase);
		vbase = s->as_vbase;
		vtop = vbase + s->as_npages * PAGE_SIZE;
		if (faultaddress >= vbase && faultaddress < vtop) {
			faultadd = faultaddress;
			permis = s->as_permissions;
			break;
		}
		s = s->as_next_region;
	}
	
	if (faultadd == 0) {
		vtop = USERSTACK;
		vbase = vtop - VM_STACKPAGES * PAGE_SIZE;
		if (faultaddress >= vbase && faultaddress < vtop) {
			faultadd = faultaddress;
			// Stack is readable, writable but not executable
			permis |= (PF_W | PF_R);
		}
		
		// faultaddress is not within any range of the regions and stack
		if (faultadd == 0) {
			return EFAULT;
		}
	}
	
	index1 = (faultaddress & TOP_TEN) >> 22;
	index2 = (faultaddress & MID_TEN) >> 12;

	vaddr1 = (vaddr_t *)(as->as_pagetable + index1 * 4);
	if (*vaddr1) {
		vaddr2 = (vaddr_t *)(*vaddr1 + index2 * 4);
		// If the mapping exits in page table,
		// get the address stores in PTE, 
		// translate it into physical address, 
		// check writeable flag,
		// and prepare the physical address for TLBLO
		if (*vaddr2 & PTE_VALID) {
			vaddr = *vaddr2 & PAGE_FRAME;
			paddr = KVADDR_TO_PADDR(vaddr);
			if (permis & PF_W) {
				paddr |= TLBLO_DIRTY;
			}
		}
		// If not exists, do the mapping, 
		// update the PTE of the second page table,
		// check writeable flag,
		// and prepare the physical address for TLBLO
		else {
			vaddr = alloc_kpages(1);
			KASSERT(vaddr != 0);
			
			as_zero_region(vaddr, 1);
			*vaddr2 |= (vaddr | PTE_VALID);
			
			paddr = KVADDR_TO_PADDR(vaddr);
			if (permis & PF_W) {
				paddr |= TLBLO_DIRTY;
			}
		}
	}
	// If second page table even doesn't exists, 
	// create second page table,
	// do the mapping,
	// update the PTE,
	// and prepare the physical address.
	else {
		*vaddr1 = alloc_kpages(1);
		KASSERT(*vaddr1 != 0);
		as_zero_region(*vaddr1, 1);
		
		vaddr2 = (vaddr_t *)(*vaddr1 + index2 * 4);
		vaddr = alloc_kpages(1);
		KASSERT(vaddr != 0);
		as_zero_region(vaddr, 1);
		*vaddr2 |= (vaddr | PTE_VALID);

		paddr = KVADDR_TO_PADDR(vaddr);
		if (permis & PF_W) {
			paddr |= TLBLO_DIRTY;
		}
	}
		
	spl = splhigh();
	
	// update TLB entry
	// if there still a empty TLB entry, insert new one in
	// if not, randomly select one, throw it, insert new one in
	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_VALID;
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}
	
	// FIXME, TLB replacement algo.
	ehi = faultaddress;
	elo = paddr | TLBLO_VALID;
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
}

/*
 * SMP-specific functions.  Unused in our configuration.
 */

void
vm_tlbshootdown_all(void)
{
	panic("vm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

