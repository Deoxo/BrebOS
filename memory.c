#include "memory.h"
#include "lib/stdio.h"

extern void boot_page_directory();

extern void boot_page_table1();

unsigned int* asm_pdt = (unsigned int*) boot_page_directory;
pdt_t* pdt = (pdt_t*) boot_page_directory;
page_table_t* asm_pt1 = (page_table_t*) boot_page_table1;
page_table_t* page_tables;

unsigned int page_bitmap[32 * 1024];
unsigned int lowest_free_page_entry;
unsigned int lowest_free_page;

memory_header base = {.s = {&base, 0}};
memory_header* freep; /* Lowest free block */

unsigned int loaded_grub_modules = 0;
GRUB_module* grub_modules;

extern void stack_top();

unsigned int* stack_top_ptr = (unsigned int*) stack_top;

/** Tries to allocate a contiguous block of memory
 * @param n Size of the block in bytes
 *  @return A pointer to a contiguous block of n bytes or NULL if memory is full
 */
void* sbrk(unsigned int n);

void init_page_bitmap();

/** Tries to allocate a contiguous block of (virtual) memory
 *
 * @param n Size to allocate. Unit: sizeof(mem_header)
 * @return Pointer to allocated memory if allocation was successful, NULL otherwise
 */
memory_header* more_kernel(unsigned int n);

/** Reload cr3 which will acknowledge every pte change and invalidate TLB */
extern void reload_cr3();

/** Allocate 1024 pages to store the 1024 page tables required to map all the memory. \n
 * 	The page table that maps kernel pages is moved into the newly allocated array of page tables and then freed. \n
 * 	This function also set page_tables to point to the array of newly allocated page tables
 * 	@details
 *  Page tables will be written in physical pages 0 to 1023. \n
 *	Page tables are virtually located on pages 1024*769 to 1024*769+1023. \n
 *	@note Any call to malloc will never return 0 on a successful allocation, as virtual address 0 maps the first page
 *	table, and will consequently never be returned by malloc if allocation is successful. \n
 *	NULL will then always be considered as an invalid address.
 *	@warning This function shall only be called once
 */
void allocate_page_tables();

/** Mark the pages where GRUB module is loaded as allocated
 *
 * @param multibootInfo GRUB multiboot struct pointer
 */
void load_grub_modules(struct multiboot_info* multibootInfo);

/**
 * Load a flat binary file in memory
 * @param module GRUB module id
 * @return process instance
 */
process* load_binary(unsigned int module, GRUB_module* grub_modules);

/**
 * Load an elf in memory
 * @param module GRUB module id
 * @return process instance
 */
process* load_elf(unsigned int module, GRUB_module* grub_modules);

unsigned int get_free_page()
{
	unsigned int page = lowest_free_page;
	unsigned int i = lowest_free_page + 1;
	while (i < PDT_ENTRIES * PT_ENTRIES && PAGE_USED(i))
		i++;
	lowest_free_page = i;

	return page;
}

unsigned int get_free_page_entry()
{
	unsigned int page_entry = lowest_free_page_entry;
	unsigned int i = lowest_free_page_entry + 1;
	while (i < PDT_ENTRIES * PT_ENTRIES && PTE(i) & PAGE_PRESENT)
		i++;
	lowest_free_page_entry = i;

	return page_entry;
}

void init_page_bitmap()
{
	for (int i = 0; i < 32 * 1024; ++i)
		page_bitmap[i] = 0;

	for (int i = 0; i < PT_ENTRIES; i++)
	{
		if (!(asm_pt1->entries[i] & PAGE_PRESENT))
			continue;

		unsigned int page_id = asm_pt1->entries[i] >> 12;
		page_bitmap[i / 32] |= 1 << (page_id % 32);
		i++;
	}

	lowest_free_page = 0;
	for (; asm_pt1->entries[lowest_free_page] & PAGE_PRESENT; lowest_free_page++);
	printf_info("Kernel spans over %u pages", lowest_free_page);

	if (lowest_free_page == PT_ENTRIES)
		printf_error("Kernel needs more space than available in 1 page table\n");
}

void allocate_page_tables()
{
	// Save for later use
	unsigned int asm_pt1_page_table_entry = ((unsigned int) asm_pt1 >> 12) & 0x3FF;

	// Allocate page 1024 + 769
	unsigned int new_page_phys_id = PDT_ENTRIES + 769;
	unsigned int new_page_phys_addr = PAGE_ID_PHYS_ADDR(new_page_phys_id);

	// Map it in entry 1022 of asm pt
	asm_pt1->entries[1022] = new_page_phys_addr | PAGE_WRITE | PAGE_PRESENT;
	MARK_PAGE_AS_ALLOCATED(new_page_phys_id);

	// Apply changes
	__asm__("invlpg (%0)" : : "r" (VIRTUAL_ADDRESS(768, 1022, 0)));

	// Newly allocated page will be the first of the page tables. It will map itself and the 1023 other page tables.
	// Get pointer to newly allocated page, casting to page_table*.
	page_tables = (page_table_t*) VIRTUAL_ADDRESS(768, 1022, 0);

	// Allocate pages 1024 to 2047, ie allocate space of all the page tables
	for (unsigned int i = 0; i < PT_ENTRIES; ++i)
		allocate_page(PDT_ENTRIES + i, i);

	// Indicate that newly allocated page is a page table. Map it in pdt[769]
	pdt->entries[769] = asm_pt1->entries[1022];
	asm_pt1->entries[1022] = 0; // Not needed anymore as the page it maps now maps itself as it became a page table

	// Apply changes
	__asm__("invlpg (%0)" : : "r" (VIRTUAL_ADDRESS(768, 1022, 0)));
	__asm__("invlpg (%0)" : : "r" (VIRTUAL_ADDRESS(769, 1022, 0)));

	// Update pointer using pdt[769]
	page_tables = (page_table_t*) VIRTUAL_ADDRESS(769, 0, 0);

	// Clear unused pages
	for (int i = 0; i < PDT_ENTRIES; ++i) // Skip first page as it maps all the page tables' pages
	{
		pdt->entries[i] |= PAGE_USER; // Let user access all page tables. Access will be defined in page table entries
		if (i == 769)
			continue;
		for (int j = 0; j < PT_ENTRIES; ++j)
			page_tables[i].entries[j] = 0;
	}

	// Copy asm_pt1 (the page table that maps the pages used by the kernel) in appropriated page table
	for (int i = 0; i < PT_ENTRIES; ++i)
		page_tables[768].entries[i] = asm_pt1->entries[i];

	for (int i = 0; i < 768; ++i)
		pdt->entries[i] = PTE_PHYS_ADDR(i) | PAGE_PRESENT | PAGE_WRITE;

	// Update pdt[entry] to point to new location of kernel page table
	pdt->entries[768] = PAGE_ID_PHYS_ADDR((PDT_ENTRIES + 768)) | (pdt->entries[768] & 0x3FF);

	for (int i = 770; i < PDT_ENTRIES; ++i)
		pdt->entries[i] = PTE_PHYS_ADDR(i) | PAGE_PRESENT | PAGE_WRITE;

	// Deallocate asm_pt1's page
	page_tables[768].entries[asm_pt1_page_table_entry] = 0;

	reload_cr3(); // Apply changes | Full TLB flush is needed because we modified every pdt entry

	lowest_free_page_entry = 770 * PDT_ENTRIES; // Kernel is (for now at least) only allowed to allocate in high half
}

void load_grub_modules(struct multiboot_info* multibootInfo)
{
	grub_modules = malloc(multibootInfo->mods_count * sizeof(GRUB_module));

	for (unsigned int i = 0; i < multibootInfo->mods_count; ++i)
	{
		multiboot_module_t* module = &((multiboot_module_t*) (multibootInfo->mods_addr + 0xC0000000))[i];

		// Set module page as present
		unsigned int module_phys_start_page_id = (unsigned int) module->mod_start / PAGE_SIZE;
		unsigned int mod_size = module->mod_end - module->mod_start;
		unsigned int required_pages = mod_size / PAGE_SIZE + (mod_size % PAGE_SIZE == 0 ? 0 : 1);

		unsigned int page_entry_id = lowest_free_page_entry;

		grub_modules[i].start_addr = ((page_entry_id / PDT_ENTRIES) << 22) | ((page_entry_id % PDT_ENTRIES) << 12);
		grub_modules[i].size = mod_size;

		// Mark module's code pages as allocated and user accessible
		for (unsigned int j = 0; j < required_pages; ++j)
		{
			unsigned int phys_page_id = module_phys_start_page_id + j;
			allocate_page_user(phys_page_id, page_entry_id);

			page_entry_id++;
		}

		lowest_free_page_entry = page_entry_id;
	}

	loaded_grub_modules = multibootInfo->mods_count;
}

void init_mem(struct multiboot_info* multibootInfo)
{
	init_page_bitmap();
	allocate_page_tables();
	freep = &base;
	load_grub_modules(multibootInfo);
}

void* sbrk(unsigned int n)
{
	// Memory full
	if (lowest_free_page == PT_ENTRIES * PDT_ENTRIES)
		return 0x00;

	unsigned int b = lowest_free_page_entry; /* block beginning page index */
	unsigned int e; /* block end page index + 1*/
	unsigned num_pages_requested = n / PAGE_SIZE + (n % PAGE_SIZE == 0 ? 0 : 1);;

	// Try to find contiguous free virtual block of memory
	while (1)
	{
		// Maximum possibly free memory is too small to fulfill request
		if (b + num_pages_requested > PDT_ENTRIES * PT_ENTRIES)
			return 0x00;

		unsigned int rem = num_pages_requested;
		unsigned int j = b;

		// Explore contiguous free blocks while explored block size does not fulfill the request
		while (!(PTE(b) & PAGE_PRESENT) && rem > 0)
		{
			j++;
			rem -= 1;
		}

		// We have explored a free block that is big enough
		if (rem == 0)
		{
			e = j;
			break;
		}

		// There is a free virtual memory block from b to j - 1 that is too small. Page entry j is present.
		// Then next possibly free page entry is the (j + 1)th
		b = j + 1;
	}

	// Allocate pages
	for (unsigned int i = b; i < e; ++i)
		allocate_page(get_free_page(), i);

	lowest_free_page_entry = e;
	while (PTE(lowest_free_page_entry) & PAGE_PRESENT)
		lowest_free_page_entry++;

	// Allocated memory block virtually starts at page b. Return it.
	return (void*) (((b / PDT_ENTRIES) << 22) | ((b % PDT_ENTRIES) << 12));
}

memory_header* more_kernel(unsigned int n)
{
	if (n < N_ALLOC)
		n = N_ALLOC;

	memory_header* h = sbrk(sizeof(memory_header) * n);

	if ((int*) h == 0x00)
		return 0x00;

	h->s.size = n;

	free((void*) (h + 1));

	return freep;
}

void* malloc(unsigned int n)
{
	memory_header* c;
	memory_header* p = freep;
	unsigned nunits = (n + sizeof(memory_header) - 1) / sizeof(memory_header) + 1;

	for (c = p->s.ptr;; p = c, c = c->s.ptr)
	{
		if (c->s.size >= nunits)
		{
			if (c->s.size == nunits)
				p->s.ptr = c->s.ptr;
			else
			{
				c->s.size -= nunits;
				c += c->s.size;
				c->s.size = nunits;
			}
			freep = p;
			return (void*) (c + 1);
		}
		if (c == freep) /* wrapped around free list */
		{
			if ((c = more_kernel(nunits)) == 0x00)
				return 0x00; /* none left */
		}
	}
}

void free(void* ptr)
{
	memory_header* c = (memory_header*) ptr - 1;
	memory_header* p;

	// Loop until p < c < p->s.ptr
	for (p = freep; !(c > p && c < p->s.ptr); p = p->s.ptr)
		//Break when arrived at the end of the list and c goes before beginning or after end

		if (p >= p->s.ptr && (c < p->s.ptr || c > p))
			break;

	// Join with upper neighbor
	if (c + c->s.size == p->s.ptr)
	{
		c->s.size += p->s.ptr->s.size;
		c->s.ptr = p->s.ptr->s.ptr;
	}
	else
		c->s.ptr = p->s.ptr;

	// Join with lower neighbor
	if (p + p->s.size == c)
	{
		p->s.size += c->s.size;
		p->s.ptr = c->s.ptr;
	}
	else
		p->s.ptr = c;

	// Set new freep
	freep = p;
}

void allocate_page(unsigned int phys_page_id, unsigned int page_id)
{
	// Compute indexes
	unsigned int pde = page_id / PDT_ENTRIES;
	unsigned int pte = page_id % PDT_ENTRIES;

	// Write PTE
	page_tables[pde].entries[pte] = PAGE_ID_PHYS_ADDR(phys_page_id) | PAGE_WRITE | PAGE_PRESENT;
	MARK_PAGE_AS_ALLOCATED(phys_page_id); // Internal allocation registration
}

void free_page(unsigned int page_id)
{
	// Compute indexes
	unsigned int pde = page_id / PDT_ENTRIES;
	unsigned int pte = page_id % PDT_ENTRIES;

	unsigned int phys_page_id = PTE(page_id) >> 12;

	// Write PTE
	page_tables[pde].entries[pte] = 0;
	__asm__("invlpg (%0)" : : "r" (VIRTUAL_ADDRESS(pde, pte, 0))); // Invalidate TLB entry
	MARK_PAGE_AS_FREE(phys_page_id); // Internal deallocation registration
}

void allocate_page_user(unsigned int phys_page_id, unsigned int page_id)
{
	// Compute indexes
	unsigned int pde = page_id / PDT_ENTRIES;
	unsigned int pte = page_id % PDT_ENTRIES;

	// Write PTE
	page_tables[pde].entries[pte] = PAGE_ID_PHYS_ADDR(phys_page_id) | PAGE_USER | PAGE_WRITE | PAGE_PRESENT;
	MARK_PAGE_AS_ALLOCATED(phys_page_id); // Internal allocation registration
}

void* page_aligned_malloc(unsigned int size)
{
	unsigned int total_size = size + sizeof(void*) + PAGE_SIZE;
	void* base_addr = malloc(total_size);

	unsigned int aligned_addr = ((unsigned int) base_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	((void**) aligned_addr)[-1] = base_addr;

	return (void*) aligned_addr;
}

void page_aligned_free(void* ptr)
{
	void* base_addr = ((void**) ptr)[-1];
	free(base_addr);
}

pdt_t* get_pdt()
{
	return pdt;
}

page_table_t* get_page_tables()
{
	return page_tables;
}

GRUB_module* get_grub_modules()
{
	return grub_modules;
}

unsigned int* get_stack_top_ptr()
{
	return stack_top_ptr;
}
