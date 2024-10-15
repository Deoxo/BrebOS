#ifndef INCLUDE_MEMORY_H
#define INCLUDE_MEMORY_H

#include "multiboot.h"

#define PAGE_SIZE 4096
#define PAGE_PRESENT 0x1
#define PAGE_WRITE   0x2
#define PAGE_USER    0x4

#define PDT_ENTRIES 1024
#define PT_ENTRIES 1024

/* Minimum memory block size to allocate to reduce calls to sbrk */
#define N_ALLOC 1024
#define FREE_THRESHOLD (10 * PAGE_SIZE) // Maximum bytes freed before attempting to free pages

#define STACK_SIZE 4096

#define FRAME_ID_ADDR(i) (i * PAGE_SIZE)
#define VIRT_ADDR(pde, pte, offset) (pde << 22 | pte << 12 | offset)
#define PTE_PHYS_ADDR(i) (FRAME_ID_ADDR((PDT_ENTRIES + i)))
#define PTE_USED(i) (PTE(i) & PAGE_PRESENT)
#define PTE(i) (page_tables[i / PDT_ENTRIES].entries[i % PDT_ENTRIES])
#define FRAME_USED(i) frame_bitmap[i / 32] & (1 << (i % 32))
#define FRAME_FREE(i) !(FRAME_USED(i))
#define MARK_FRAME_USED(i) frame_bitmap[i / 32] |= 1 << (i % 32)
#define MARK_FRAME_FREE(i) frame_bitmap[i / 32] &= ~(1 << (i % 32))
#define PHYS_ADDR(virt_addr) (page_tables[virt_addr >> 22].entries[(virt_addr >> 12) & 0x3FF] & ~0x3FF)

//https://wiki.osdev.org/Paging

typedef struct
{
	unsigned int entries[PT_ENTRIES];
} page_table_t;

typedef struct
{
	unsigned int entries[PDT_ENTRIES];
} pdt_t;

typedef double Align;

typedef union memory_header /* memory block header */
{
	struct
	{
		union memory_header* ptr; /* next block */
		unsigned int size; /* size of this block */
	} s;
	Align x; /* force alignment of blocks */
} memory_header;

typedef struct
{
	unsigned int start_addr, size;
} GRUB_module;

/** Initialize memory, by referencing free pages, allocating pages to store 1024 pages tables
 *
 * @param minfo Multiboot info structure
 */
void init_mem(multiboot_info_t* minfo);

/** Tries to allocate a contiguous block of memory
 *
 * @param n Size of the block in size
 * @return Address of beginning of allocated block if allocation was successful, NULL otherwise
 */
extern "C" void* malloc(unsigned int n);

/**
 * Allocate page-aligned memory
 *
 * @param size Size of memory to allocate
 *
 * @return Pointer to page-aligned memory
 */
void* page_aligned_malloc(unsigned int size);

/** Frees some memory
 *
 * @param ptr Pointer to the memory block to free
 */
extern "C" void free(void* ptr);

/**
 * Free page-aligned memory
 *
 * @param ptr Pointer to page-aligned memory to free
 */
void page_aligned_free(void* ptr);

/** Allocate a page
 *
 * @param frame_id Physical page id
 * @param page_id Page id
 */
void allocate_page(unsigned int frame_id, unsigned int page_id);

/** Allocate a page with user permissions
 *
 * @param frame_id Physical page id
 * @param page_id Page id
 */
void allocate_page_user(unsigned int frame_id, unsigned int page_id);

/**
 * Free a page
 * @param pde PDE
 * @param pte PTE
 */
void free_page(unsigned int pde, unsigned int pte);

/** Get index of lowest free page id and update lowest_free_page to next free page id */
unsigned int get_free_page();

/** Get index of lowest free page entry id and update lowest_free_pe to next free page id */
unsigned int get_free_pe();

/** Get pdt */
pdt_t* get_pdt();

/** Get page tables */
page_table_t* get_page_tables();

/** Get grub modules */
GRUB_module* get_grub_modules();

/** Get pointer to top of kernel stack */
unsigned int* get_stack_top_ptr();

#endif //INCLUDE_MEMORY_H
