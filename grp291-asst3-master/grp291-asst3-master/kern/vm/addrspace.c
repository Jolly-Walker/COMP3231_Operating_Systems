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
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */
int copy_pagetable(paddr_t ***old, paddr_t ***new);

int copy_pagetable(paddr_t ***old, paddr_t ***new){
	if (new == NULL || old == NULL) {
		return EFAULT;
	}
	// traverse root nodes
	for (int i = 0; i < PT_ROOT_SIZE; i++){
		if (old[i] == NULL){
			new[i] = NULL;
			continue;
		}
		new[i] = kmalloc(sizeof(paddr_t *) * PT_NODE_SIZE);
		if (new[i] == NULL){ return ENOMEM; }

		// traverse second level nodes
		for (int j = 0; j < PT_NODE_SIZE; j++){
			if (old[i][j] == NULL){
				new[i][j] = NULL;
				continue;
			}
			new[i][j] = kmalloc(sizeof(paddr_t) * PT_NODE_SIZE);
			if (new[i][j] == NULL){ return ENOMEM; }

			// traverse leaf nodes
			for (int k = 0; k < PT_NODE_SIZE; k++){
				if (old[i][j][k] == 0) {
					new[i][j][k] = 0;
					continue;
				}

				// allocate a physical frame
				vaddr_t kern_addr = alloc_kpages(1);
				if (kern_addr == 0){ return ENOMEM; }
				paddr_t frame_addr = KVADDR_TO_PADDR(kern_addr);
				new[i][j][k] = frame_addr;

				// copy over the content
				memcpy((void *)kern_addr, (const void *)PADDR_TO_KVADDR(old[i][j][k]), PAGE_SIZE);
			}
		}
	}
	return 0;
}

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */
	as->stack = USERSTACK;
	as->head = NULL;

	as->pagetable = kmalloc(sizeof(paddr_t **) * PT_ROOT_SIZE);
	if (as->pagetable == NULL) {
		return NULL;
	}
	for (int i = 0; i < PT_ROOT_SIZE; ++i) {
		as->pagetable[i] = NULL;
	}
	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	// copy old region list
	struct region_list *cur = newas->head;
	struct region_list *tmp;
	struct region_list *old_cur = old->head;
	while(old_cur != NULL) {
		tmp = kmalloc(sizeof(struct region_list));
		if (tmp == NULL) {
			return ENOMEM;
		}
		tmp->size = old_cur->size;
		tmp->base = old_cur->base;
		tmp->flag = old_cur->flag;
		tmp->next = NULL;

		if (newas->head == NULL) {
			newas->head = tmp;
			cur = tmp;
		} else {
			cur->next = tmp;
			cur = tmp;
		}
		old_cur = old_cur->next;
	}

	// copy the old pagetable
	int err = copy_pagetable(old->pagetable, newas->pagetable);
	if (err) {
		return ENOMEM;
	}
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	// destroy linked list

	if (as->head != NULL) {
		struct region_list *tmp;
		struct region_list *cur = as->head;
		while (cur != NULL) {
			tmp = cur->next;
			kfree(cur);
			cur = tmp;
		}
	}
	// destroy 3 level page table
	if (as->pagetable != NULL) {
		for (int i = 0 ; i < PT_ROOT_SIZE; ++i) {
			if (as->pagetable[i] != NULL) {
				for (int j = 0; j < PT_NODE_SIZE; ++j) {
					if (as->pagetable[i][j] != NULL) {
						for (int k = 0; k < PT_NODE_SIZE ; ++k) {
							if (as->pagetable[i][j][k] != 0) {
								free_kpages(PADDR_TO_KVADDR(as->pagetable[i][j][k]));
							}
						}
						kfree(as->pagetable[i][j]);
					}
				}
				kfree(as->pagetable[i]);
			}
		}
		kfree(as->pagetable);
	}
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}
	
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */
	if (as == NULL) {
		return EFAULT;
	}
	struct region_list *new_region = kmalloc(sizeof(struct region_list));
	if (new_region == NULL) {
		return ENOMEM;
	}

	new_region->base = vaddr;
	new_region->size = memsize;
	int flag_val = 0;
	if (readable) {
	 	flag_val = flag_val | READ_FLAG;
	}
	if (writeable) {
	 	flag_val = flag_val | WRITE_FLAG;
	}
	if (executable) {
		flag_val = flag_val | EXEC_FLAG;
	}
	new_region->flag = flag_val;
	new_region->next = as->head;
	as->head = new_region;
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	if (as == NULL) {
		return EFAULT;
	}
	// if change read_only regions to RW region and set 0x8 to be true
	struct region_list *cur = as->head;
	while (cur != NULL) {
		if (cur->flag == READ_FLAG) {
			cur->flag = cur->flag | LOAD_FLAG;
			cur->flag = cur->flag | WRITE_FLAG;
		}
		cur = cur->next;
	}
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	// if 0x8 mask is true, change it to read_only
	struct region_list *cur = as->head;
	while (cur != NULL) {
		if (cur->flag & LOAD_FLAG) {
			cur->flag = READ_FLAG;
		}
		cur = cur->next;
	}
	
	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	for (int i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */
	vaddr_t stack_base = USERSTACK - STACK_PAGE * PAGE_SIZE;
	as_define_region(as, stack_base, STACK_PAGE, 1, 1, 0);
	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

