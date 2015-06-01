/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

/*
 * Create addrspace for current process.
 * Alloc a free frame for first level page table and zero out the new page
 */
struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	// create first level page table
	as->as_pagetable = alloc_kpages(1);
	if (as->as_pagetable == 0) {
		kprintf("Can't create page table. \n");
		return NULL;
	}
	
	as_zero_region(as->as_pagetable, 1);
	as->as_regions_start = 0;
	return as;
}

/*
 * Copy all the contents of the old addrspace to the new addrspace.
 * Both the page frames mapped in the two-level page table of
 * the old addrspace to the new addrspace and the regions keeped in 
 * the old one.
 */
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;
	struct as_region *s, *news, *olds;
	vaddr_t *ovaddr1, *ovaddr2, *nvaddr1, *nvaddr2, vaddr;

	// initialise the new addrspace
	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	// copy the information(vbase, npages and permission) of
	// the regions of the old addrspace into the new one
	s = old->as_regions_start;
	if (s == 0) {
		return 0;
	}
	
	news = kmalloc(sizeof(struct as_region));
	KASSERT(s->as_vbase != 0);
	KASSERT(s->as_npages != 0);
	news->as_vbase = s->as_vbase;
	news->as_npages = s->as_npages;
	news->as_permissions = s->as_permissions;
	news->as_next_region = 0;
	new->as_regions_start = news;
	s = s->as_next_region;
	
	while (s != 0) {
		olds = news;
		news = kmalloc(sizeof(struct as_region));
		KASSERT(s->as_vbase != 0);
		KASSERT(s->as_npages != 0);
		news->as_vbase = s->as_vbase;
		news->as_npages = s->as_npages;
		news->as_permissions = s->as_permissions;
		news->as_next_region = 0;
		olds->as_next_region = news;
		s = s->as_next_region;
	}
	
	// copy the contents of the old two-level page table
	// to the new one
	ovaddr1 = (vaddr_t *) old->as_pagetable;
	nvaddr1 = (vaddr_t *) new->as_pagetable;
	for (int i = 0; i < PTE_NUM; ++i) {
		if (*ovaddr1) {
			*nvaddr1 = alloc_kpages(1);
			KASSERT(*nvaddr1 != 0);
			// zero out the new allocated page
			as_zero_region(*nvaddr1, 1);

			ovaddr2 = (vaddr_t *)(*ovaddr1);
			nvaddr2 = (vaddr_t *)(*nvaddr1);
			for (int j = 0; j < PTE_NUM; ++j) {
				// copy old page table content 
				// if it actually contains the volid address of the physical frame
				if (*ovaddr2 & PTE_VALID) {
					vaddr = alloc_kpages(1);
					KASSERT(vaddr != 0);
					// copy all the contents of the old frame to the new frame
					memmove((void *)vaddr,
							 (const void *)(*ovaddr2 & PAGE_FRAME),
							PAGE_SIZE);
					// update the PTE of the new addrspace's page table
					*nvaddr2 = (vaddr | PTE_VALID);
				}
				ovaddr2 += 1;
				nvaddr2 += 1;
			}
		}
		ovaddr1 += 1;
		nvaddr1	+= 1;
	}
	
	*ret = new;
	return 0;
}

/*
 * free all the memory space used in addrspace
 * including the frames used for the two-level page table;
 * the space used for regions information storeage;
 * and the space used for the addrspace itself
 */
void
as_destroy(struct addrspace *as)
{
	vaddr_t *vaddr1, *vaddr2, vaddr;
	
	KASSERT(as != NULL);
	
	vaddr1 = (vaddr_t *) as->as_pagetable;
	
	for (int i = 0; i < PTE_NUM; ++i) {
		if (*vaddr1) {
			vaddr2 = (vaddr_t *)(*vaddr1);
			for (int j = 0; j < PTE_NUM; ++j) {
				if (*vaddr2 & PTE_VALID) {
					vaddr = *vaddr2 & PAGE_FRAME;
					free_kpages(vaddr);
				}
				vaddr2 += 1;
			}
			free_kpages(*vaddr1);
		}
		vaddr1 += 1;
	}
	free_kpages(as->as_pagetable);
	
	KASSERT(as->as_regions_start != 0);
	as_destroy_regions(as->as_regions_start);
	kfree(as);
}

/*
 * Recusively free the memory space used for regions information storeage
 */
void
as_destroy_regions(struct as_region *ar)
{
	if (ar != 0) {
		as_destroy_regions(ar->as_next_region);
		kfree(ar);
	}
}

/*
 * Frobe TLB table
 */
void
as_activate(struct addrspace *as)
{
	(void)as;
	int spl = splhigh();
	for (int i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. 
 *
 * Set up the second page table for the provide virtual address
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t index1, index2, npages;
	vaddr_t *vaddr1, *vaddr2;
	struct as_region *ar;
	
	KASSERT(as != NULL);
	
	// Align the region. First, the base...
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	
	// ...and now the length.
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;	
	npages = sz / PAGE_SIZE;
	
	// Store the region base, size and permissions
	if (as->as_regions_start == 0) {
		as->as_regions_start = kmalloc(sizeof(struct as_region));
		as->as_regions_start->as_vbase = vaddr;
		as->as_regions_start->as_npages = npages;
		as->as_regions_start->as_permissions = readable | writeable | executable;
		as->as_regions_start->as_next_region = 0;
	}
	else {
		ar = as->as_regions_start;
		while (ar->as_next_region != 0) {
			ar = ar->as_next_region;
		}
		ar->as_next_region = kmalloc(sizeof(struct as_region));
		ar->as_next_region->as_vbase = vaddr;
		ar->as_next_region->as_npages = npages;
		ar->as_next_region->as_permissions = readable | writeable | executable;
		ar->as_next_region->as_next_region = 0;
	}
	
	// mapped the virtual address to the second-level page table
	for (size_t i = 0; i < npages; ++i) {
		index1 = (vaddr & TOP_TEN) >> 22;
		vaddr1 = (vaddr_t *)(as->as_pagetable + index1 * 4);
		if (*vaddr1 == 0) {
			*vaddr1 = alloc_kpages(1);
			if (*vaddr1 == 0) {
				return EFAULT;
			}
			as_zero_region(*vaddr1, 1);
		}
		index2 = (vaddr & MID_TEN) >> 12;
		vaddr2 = (vaddr_t *)(*vaddr1 + index2 *4);
		*vaddr2 = 0;
		vaddr += PAGE_SIZE;
	}
	return 0;
}

/*
 * Assign the writable permission to all region in order for os 
 * to load segment into physical frame
 */
int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as != NULL);
	KASSERT(as->as_regions_start != 0);
	
	struct as_region *s;
	unsigned int permis = 0;
	
	s = as->as_regions_start;
	while (s != 0) {
		permis = s->as_permissions;
		// in order to restore the permission flag back when 
		// the load execution done, move the original permission
		// 8 bit left to reserve it. Then put the new permission flag
		// into the last 8 bit
		s->as_permissions <<= 8;
		s->as_permissions = s->as_permissions | permis | PF_W;
		s = s->as_next_region;
	}
	return 0;
}

/*
 * Restore the original region permission flag back
 */
int
as_complete_load(struct addrspace *as)
{
	KASSERT(as != NULL);
	KASSERT(as->as_regions_start != 0);
	
	struct as_region *s;
	s = as->as_regions_start;
	while (s != 0) {
		s->as_permissions >>= 8;
		s = s->as_next_region;
	}
	return 0;
}

/*
 * Set up the mapping from stack virtual address to the page table
 */
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	int index1, index2;
	vaddr_t vaddr, *vaddr1, *vaddr2;
	for (int i = 0; i <VM_STACKPAGES; ++i) {
		vaddr = USERSTACK - i * PAGE_SIZE;
		
		index1 = (vaddr & TOP_TEN) >> 22;
		vaddr1 = (vaddr_t *)(as->as_pagetable + index1 * 4);
		if (*vaddr1 == 0) {
			*vaddr1 = alloc_kpages(1);
			if (*vaddr1 == 0) {
				return EFAULT;
			}
			as_zero_region(*vaddr1, 1);
		}
		index2 = (vaddr & MID_TEN) >> 12;
		vaddr2 = (vaddr_t *)(*vaddr1 + index2 * 4);
		*vaddr2 = 0;
	}
	
	// Initial user-level stack pointer
	*stackptr = USERSTACK;
	
	return 0;
}

/*
 * Zero out a page within the provide address
 */
void
as_zero_region(vaddr_t vaddr, unsigned npages)
{
	bzero((void *)vaddr, npages * PAGE_SIZE);
}
