#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <proc.h>
#include <spl.h>

/* Place your page table functions here */
paddr_t get_frame(vaddr_t page_addr, struct addrspace *as);
int add_PTE(vaddr_t page_addr, paddr_t frame_addr, struct addrspace *as);
int in_valid_region(struct addrspace *as, vaddr_t page_addr);

// get physical address stored on corresponding leaf node
paddr_t get_frame(vaddr_t page_addr, struct addrspace *as){
    vaddr_t root_page = (page_addr & ROOT_PAGE) >> 24;
    if (as->pagetable[root_page] == NULL){ return 0; }
    
    vaddr_t sec_page = (page_addr & SEC_LVL_PAGE) >> 18;
    if (as->pagetable[root_page][sec_page] == NULL){ return 0; }

    vaddr_t leaf_page = (page_addr & LEAF_PAGE) >> 12;
    return as->pagetable[root_page][sec_page][leaf_page];
}

int add_PTE(vaddr_t page_addr, paddr_t frame_addr, struct addrspace *as) {
    vaddr_t root_page = (page_addr & ROOT_PAGE) >> 24;
    if (as->pagetable[root_page] == NULL){ // need a new root page
        as->pagetable[root_page] = kmalloc(PT_NODE_SIZE * sizeof(paddr_t *));
        for (int i = 0; i < PT_NODE_SIZE; i++){
            as->pagetable[root_page][i] = NULL;
        }
    }
    vaddr_t sec_page = (page_addr & SEC_LVL_PAGE) >> 18;
    if (as->pagetable[root_page][sec_page] == NULL){
        as->pagetable[root_page][sec_page] = kmalloc(PT_NODE_SIZE * sizeof(paddr_t));
        for (int i = 0; i < PT_NODE_SIZE; i++){
            as->pagetable[root_page][sec_page][i] = 0;
        }
    }
    vaddr_t leaf_page = (page_addr & LEAF_PAGE) >> 12;
    as->pagetable[root_page][sec_page][leaf_page] = frame_addr;
    return 0;
}

int in_valid_region(struct addrspace *as, vaddr_t page_addr){
    struct region_list *region = as->head;
    while (region != NULL){
        vaddr_t vbase = region->base;
        vaddr_t vtop = region->base + region->size * PAGE_SIZE;
        if (page_addr >= vbase && page_addr < vtop){
            return 0;
        }
        region = region->next;
    }
    return 1;
}

void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    (void) faulttype;
    (void) faultaddress;

    if (faultaddress == 0x0){
        return EFAULT;
    }
	faultaddress &= PAGE_FRAME;

    if (faulttype == VM_FAULT_READONLY){ 
        return EFAULT; 
    }

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

    struct addrspace *as = proc_getas();
    if (as == NULL) {
		return EFAULT;
	}

    paddr_t frame_addr = get_frame(faultaddress, as);

    // if no mapping found in page table
    if (frame_addr == 0){
        int valid = in_valid_region(as, faultaddress);
        if (valid == 1) { // region invalid
            return EFAULT; 
        } 

        // allocate a frame
        vaddr_t kern_addr = alloc_kpages(1);
        if (kern_addr == 0) { 
            return ENOMEM;
        }
        frame_addr = KVADDR_TO_PADDR(kern_addr); 
        bzero((void *)kern_addr, PAGE_SIZE);
        int res = add_PTE(faultaddress, frame_addr, as); // add a new entry to page table
        if (res) {
            return EFAULT;
        }
    }

    // insert into TLB, FRAME_ADDR would be valid now 
	uint32_t ehi, elo;
    int spl = splhigh();

	// for (int i=0; i<NUM_TLB; i++) {
	// 	tlb_read(&ehi, &elo, i);
	// 	if (elo & TLBLO_VALID) {
	// 		continue;
	// 	}
	// 	ehi = faultaddress;
	// 	elo = frame_addr | TLBLO_DIRTY | TLBLO_VALID;
	// 	tlb_write(ehi, elo, i);
	// 	splx(spl);
	// 	return 0;
	// }

    // if TLB is full
    ehi = faultaddress;
	elo = frame_addr | TLBLO_DIRTY | TLBLO_VALID;
    tlb_random(ehi, elo);
	splx(spl);
    
    return 0;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

